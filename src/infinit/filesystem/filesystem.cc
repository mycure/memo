#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/MissingBlock.hh>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>

#include <cryptography/hash.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit.fs");

static elle::Version const version
  (INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);

namespace rfs = reactor::filesystem;

namespace infinit
{
  namespace filesystem
  {
    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::blocks::MutableBlock MutableBlock;
    typedef infinit::model::blocks::ImmutableBlock ImmutableBlock;
    typedef infinit::model::Address Address;

    class AnyBlock
    {
    public:
      AnyBlock();
      AnyBlock(std::unique_ptr<Block> block);
      AnyBlock(AnyBlock && b);
      AnyBlock(AnyBlock const& b) = delete;
      AnyBlock& operator = (const AnyBlock& b) = delete;
      void operator = (AnyBlock && b);
      Address address() {return _backend->address();}
      void
      data(std::function<void (elle::Buffer&)> transformation);
      const elle::Buffer& data();
      // Output a block of same type, address might change
      std::unique_ptr<Block> take(infinit::model::Model& model);
      Address store(infinit::model::Model& model, infinit::model::StoreMode mode);

      void zero(int offset, int count);
      void write(int offset, const void* input, int count);
      void read(int offset, void* output, int count);
    private:
      std::unique_ptr<Block> _backend;
      bool _is_mutable;
      elle::Buffer _buf;
    };

    class Directory;
    class File;
    typedef std::shared_ptr<Directory> DirectoryPtr;
    enum class FileStoreMode
    {
      direct = 0, // address points to file data
      index = 1,   // address points to index of addresses
      none = 2,    // no storage (symlink)
    };
    static Address::Value zeros = {0};
    struct FileData
    {
      std::string name;
      uint64_t size;
      uint32_t mode;
      uint32_t uid;
      uint32_t gid;
      uint64_t atime; // access:  read,
      uint64_t mtime; // content change  dir: create/delete file
      uint64_t ctime; //attribute change+content change
      Address address;
      FileStoreMode store_mode;
      boost::optional<std::string> symlink_target;

      FileData(std::string name, uint64_t size, uint32_t mode, uint64_t atime,
        uint64_t mtime, uint64_t ctime, Address const& address, FileStoreMode store_mode)
        : name(name)
        , size(size)
        , mode(mode)
        , atime(atime)
        , mtime(mtime)
        , ctime(ctime)
        , address(address)
        , store_mode(store_mode)
      {}

      FileData()
        : address(zeros)
      {}

      FileData(elle::serialization::SerializerIn& s)
        : address(zeros)
      {
        s.serialize_forward(*this);
      }

      void serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", name);
        s.serialize("size", size);
        s.serialize("mode", mode);
        s.serialize("atime", atime);
        s.serialize("mtime", mtime);
        s.serialize("ctime", ctime);
        s.serialize("address", address);
        int sm = (int)store_mode;
        s.serialize("store_mode", sm);
        store_mode = (FileStoreMode)sm;
        try {
          s.serialize("uid", uid);
          s.serialize("gid", gid);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("serialization error %s, assuming old format", e);
        }
        s.serialize("symlink_target", symlink_target);
      }
    };
    struct CacheStats
    {
      int directories;
      int files;
      int blocks;
      long size;
    };
    #define THROW_NOENT { throw rfs::Error(ENOENT, "No such file or directory");}
    #define THROW_NOSYS { throw rfs::Error(ENOSYS, "Not implemented");}
    #define THROW_EXIST { throw rfs::Error(EEXIST, "File exists");}
    #define THROW_NOSYS { throw rfs::Error(ENOSYS, "Not implemented");}
    #define THROW_ISDIR { throw rfs::Error(EISDIR, "Is a directory");}
    #define THROW_NOTDIR { throw rfs::Error(ENOTDIR, "Is not a directory");}
    #define THROW_NODATA { throw rfs::Error(ENODATA, "No data");}
    #define THROW_INVAL { throw rfs::Error(EINVAL, "Invalid argument");}
    #define THROW_ACCES { throw rfs::Error(EACCES, "Access denied");}

    class Node
    {
    protected:
      Node(FileSystem& owner, std::shared_ptr<Directory> parent, std::string const& name)
      : _owner(owner)
      , _parent(parent)
      , _name(name)
      {}
      void rename(boost::filesystem::path const& where);
      void utimens(const struct timespec tv[2]);
      void chmod(mode_t mode);
      void chown(int uid, int gid);
      void stat(struct stat* st);
      void _remove_from_cache();
      boost::filesystem::path full_path();
      FileSystem& _owner;
      std::shared_ptr<Directory> _parent;
      std::string _name;
    };
    class Unknown: public Node, public rfs::Path
    {
    public:
      Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name);
      void stat(struct stat*) override
      {
        ELLE_DEBUG("Stat on unknown %s", _name);
        THROW_NOENT;
      }
      void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override THROW_NOENT;
      void mkdir(mode_t mode) override;
      void rmdir() override THROW_NOENT;
      void rename(boost::filesystem::path const& where) override THROW_NOENT;
      boost::filesystem::path readlink() override THROW_NOENT;
      void symlink(boost::filesystem::path const& where) override;
      void link(boost::filesystem::path const& where) override;
      void chmod(mode_t mode) override THROW_NOENT;
      void chown(int uid, int gid) override THROW_NOENT;
      void statfs(struct statvfs *) override THROW_NOENT;
      void utimens(const struct timespec tv[2]) override THROW_NOENT;
      void truncate(off_t new_size) override THROW_NOENT;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOENT;
      bool allow_cache() override { return false;}
      std::string getxattr(std::string const& k) override {THROW_NODATA;}
    private:
    };

    class Symlink: public Node, public rfs::Path
    {
    public:
      Symlink(DirectoryPtr parent, FileSystem& owner, std::string const& name);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) THROW_NOTDIR;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_NOSYS;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_NOSYS;
      void unlink() override;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override THROW_NOTDIR;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override;
      void symlink(boost::filesystem::path const& where) THROW_EXIST;
      void link(boost::filesystem::path const& where) override; //copied symlink
      void chmod(mode_t mode) override THROW_NOSYS; // target
      void chown(int uid, int gid) override THROW_NOSYS; // target
      void statfs(struct statvfs *) override THROW_NOSYS;
      void utimens(const struct timespec tv[2]) THROW_NOSYS;
      void truncate(off_t new_size) override THROW_NOSYS;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
      bool allow_cache() override { return false;}
    };

    class Directory: public rfs::Path, public Node
    {
    public:
      Directory(DirectoryPtr parent, FileSystem& owner, std::string const& name,
                std::unique_ptr<MutableBlock> b);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_ISDIR;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_ISDIR;
      void unlink() override THROW_ISDIR;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override  THROW_ISDIR;
      void symlink(boost::filesystem::path const& where) override THROW_EXIST;
      void link(boost::filesystem::path const& where) override THROW_EXIST;
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override THROW_ISDIR;
      std::shared_ptr<rfs::Path> child(std::string const& name) override;
      std::string getxattr(std::string const& key) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void cache_stats(CacheStats& append);
      void serialize(elle::serialization::Serializer&);
      bool allow_cache() override { return true;}
    private:
      void _fetch();
      void move_recurse(boost::filesystem::path const& current,
                        boost::filesystem::path const& where);
      friend class Unknown;
      friend class File;
      friend class Symlink;
      friend class Node;
      friend class FileHandle;
      void _changed(bool set_mtime = false);
      void _push_changes();
      std::unique_ptr<MutableBlock> _block;
      std::unordered_map<std::string, FileData> _files;
      boost::posix_time::ptime _last_fetch;
      friend class FileSystem;
    };
    static const boost::posix_time::time_duration directory_cache_time
      = boost::posix_time::seconds(2);

    class FileHandle: public rfs::Handle
    {
    public:
      FileHandle(std::shared_ptr<File> owner,
                 bool update_folder_mtime=false,
                 bool no_prefetch = false,
                 bool mark_dirty = false);
      ~FileHandle();
      int read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      int write(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      void ftruncate(off_t offset) override;
      void close() override;
    private:
      std::shared_ptr<File> _owner;
      bool _dirty;
    };

    class File: public rfs::Path, public Node
    {
    public:
      File(DirectoryPtr parent, FileSystem& owner, std::string const& name,
                std::unique_ptr<MutableBlock> b = std::unique_ptr<MutableBlock>());
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) THROW_NOTDIR;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override THROW_NOTDIR;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override  THROW_NOENT;
      void symlink(boost::filesystem::path const& where) override THROW_EXIST;
      void link(boost::filesystem::path const& where) override;
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]);
      void truncate(off_t new_size) override;
      std::string getxattr(std::string const& name) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
      bool allow_cache() override;
      // check cached data size, remove entries if needed
      void check_cache();
    private:
      static const unsigned long default_block_size;
      static const unsigned long max_cache_size; // in blocks
      friend class FileHandle;
      friend class Directory;
      friend class Unknown;
      // A packed network-byte-ordered version of Header sits at the
      // beginning of each file's first block in index mode.
      struct Header
      { // max size we can grow is sizeof(Address)
        static const uint32_t current_version = 1;
        uint32_t version;
        uint32_t block_size;
        uint32_t links;
        uint64_t total_size;
      };
      /* Get address for given block index.
       * @param create: if true, allow creation of a new block as needed
       *                else returns nullptr if creation was required
      */
      AnyBlock*
      _block_at(int index, bool create);
      // Switch from direct to indexed mode
      void _switch_to_multi(bool alloc_first_block);
      void _changed();
      Header _header(); // Get header, must be in multi mode
      void _header(Header const&);
      bool _multi(); // True if mode is index
      struct CacheEntry
      {
        AnyBlock block;
        bool dirty;
        std::chrono::system_clock::time_point last_use;
        bool new_block;
      };
      std::unordered_map<int, CacheEntry> _blocks;
      std::unique_ptr<MutableBlock> _first_block;
      bool _first_block_new;
      int _handle_count;
    };

    const unsigned long File::default_block_size = 1024 * 1024;
    const unsigned long File::max_cache_size = 20; // in blocks

    AnyBlock::AnyBlock()
    {}
    AnyBlock::AnyBlock(std::unique_ptr<Block> block)
    : _backend(std::move(block))
    , _is_mutable(dynamic_cast<MutableBlock*>(_backend.get()))
    {
      ELLE_DEBUG("Anyblock mutable=%s", _is_mutable);
      if (!_is_mutable)
      {
        _buf = _backend->take_data();
        ELLE_DEBUG("Nonmutable, stole %s bytes", _buf.size());
      }
    }

    AnyBlock::AnyBlock(AnyBlock && b)
    : _backend(std::move(b._backend))
    , _is_mutable(b._is_mutable)
    , _buf(std::move(b._buf))
    {

    }
    void AnyBlock::operator = (AnyBlock && b)
    {
      _backend = std::move(b._backend);
     _is_mutable = b._is_mutable;
     _buf = std::move(b._buf);
    }
    void AnyBlock::data(std::function<void (elle::Buffer&)> transformation)
    {
      if (_is_mutable)
        dynamic_cast<MutableBlock*>(_backend.get())->data(transformation);
      else
        transformation(_buf);
    }
    elle::Buffer const& AnyBlock::data()
    {
      if (_is_mutable)
        return _backend->data();
      else
        return _buf;
    }
    void AnyBlock::zero(int offset, int count)
    {
      data([&](elle::Buffer& data)
        {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memset(data.mutable_contents() + offset, 0, count);
        });
    }
    void AnyBlock::write(int offset, const void* input, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(data.mutable_contents() + offset, input, count);
         });
    }
    void AnyBlock::read(int offset, void* output, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(output, data.mutable_contents() + offset, count);
         });
    }
    std::unique_ptr<Block> AnyBlock::take(infinit::model::Model& model)
    {
      if (_is_mutable)
        return std::move(_backend);
      else
        return model.make_block<ImmutableBlock>(std::move(_buf));
    }

    Address AnyBlock::store(infinit::model::Model& model,
                            infinit::model::StoreMode mode)
    {
      if (_is_mutable)
      {
        model.store(*_backend, mode);
        return _backend->address();
      }
      auto block = model.make_block<ImmutableBlock>(_buf);
      model.store(*block, mode);
      return block->address();
    }

    FileSystem::FileSystem(model::Address root,
                           std::unique_ptr<infinit::model::Model> model)
      : _root_address(root)
      , _block_store(std::move(model))
      , _single_mount(false)
    {
      reactor::scheduler().signal_handle
        (SIGUSR1, [this] { this->print_cache_stats();});
    }

    void
    FileSystem::unchecked_remove(model::Address address)
    {
      try
      {
        _block_store->remove(address);
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("Unexpected storage result: %s", mb);
      }
    }

    std::unique_ptr<model::blocks::MutableBlock>
    FileSystem::unchecked_fetch(model::Address address)
    {
      try
      {
        return elle::cast<model::blocks::MutableBlock>::runtime
          (_block_store->fetch(address));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("Unexpected storage result: %s", mb);
      }
      return {};
    }

    static
    std::unique_ptr<model::blocks::MutableBlock>
    _make_root_block(infinit::model::Model& model)
    {
      std::unique_ptr<model::blocks::MutableBlock> root
        = model.make_block<model::blocks::ACLBlock>();
      model.store(*root);
      return root;
    }

    FileSystem::FileSystem(std::unique_ptr<infinit::model::Model> model)
      : _root_address(_make_root_block(*model)->address())
      , _block_store(nullptr)
      , _single_mount(false)
    {
      ELLE_ASSERT(model.get());
      this->_block_store = std::move(model);
      ELLE_TRACE("create root block at address: %x", this->_root_address);
    }

    void
    FileSystem::print_cache_stats()
    {
      auto root = std::dynamic_pointer_cast<Directory>(filesystem()->path("/"));
      CacheStats stats;
      memset(&stats, 0, sizeof(CacheStats));
      root->cache_stats(stats);
      std::cerr << "Statistics:\n"
      << stats.directories << " dirs\n"
      << stats.files << " files\n"
      << stats.blocks <<" blocks\n"
      << stats.size << " bytes"
      << std::endl;
    }

    std::shared_ptr<rfs::Path>
    FileSystem::path(std::string const& path)
    {
      // In the infinit filesystem, we never query a path other than the
      // root.
      ELLE_ASSERT_EQ(path, "/");
      return std::make_shared<Directory>
        (nullptr, *this, "", this->_root_block());
    }

    std::unique_ptr<MutableBlock>
    FileSystem::_root_block()
    {
      return elle::cast<MutableBlock>::runtime(block_store()->fetch(_root_address));
    }

    static const int DIRECTORY_MASK = 0040000;
    static const int SYMLINK_MASK = 0120000;

    void
    Directory::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("content", this->_files);
    }

    Directory::Directory(DirectoryPtr parent, FileSystem& owner,
                         std::string const& name,
                         std::unique_ptr<MutableBlock> b)
    : Node(owner, parent, name)
    , _block(std::move(b))
    {
      ELLE_DEBUG("Directory::Directory %s, parent %s", this, parent);
      try
      {
        _block->data();
      }
      catch (elle::Error const& e)
      {
        THROW_ACCES;
      }
      if (!_block->data().empty())
      {
        ELLE_DEBUG("Deserializing directory");
        elle::IOStream is((_block->data().istreambuf()));
        elle::serialization::json::SerializerIn input(is, version);
        try
        {
          input.serialize_forward(*this);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("Directory deserialization error: %s", e);
          throw rfs::Error(EIO, e.what());
        }
      }
      else
        _changed();
    }

    void Directory::_fetch()
    {
      auto now = boost::posix_time::microsec_clock::universal_time();
      if (_last_fetch != boost::posix_time::not_a_date_time
        && now - _last_fetch < directory_cache_time)
      {
        ELLE_DEBUG("Using directory cache");
        return;
      }
      _last_fetch = now;
      _block = elle::cast<MutableBlock>::runtime
        (_owner.block_store()->fetch(_block->address()));
      std::unordered_map<std::string, FileData> local;
      std::swap(local, _files);
      ELLE_DEBUG("Deserializing directory");
      elle::IOStream is(_block->data().istreambuf());
      elle::serialization::json::SerializerIn input(is, version);
      try
      {
        input.serialize_forward(*this);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("Directory deserialization error: %s", e);
        std::swap(local, _files);
        throw rfs::Error(EIO, e.what());
      }
      // File writes update the file size in _files for reads to work,
      // but they do not commit them to store (that would be far too expensive)
      // So, keep local version of entries with bigger ctime than remote.
      for (auto const& itl: local)
      {
        auto itr = _files.find(itl.first);
        if (itr != _files.end()
          && (itr->second.ctime < itl.second.ctime
              || itr->second.mtime < itl.second.mtime))
        {
          ELLE_DEBUG("Using local data for %s", itl.first);
          itr->second = itl.second;
        }
      }
    }

    void
    Directory::statfs(struct statvfs * st)
    {
      memset(st, 0, sizeof(struct statvfs));
      st->f_bsize = 32768;
      st->f_frsize = 32768;
      st->f_blocks = 1000000;
      st->f_bavail = 1000000;
      st->f_fsid = 1;
    }

    void
    Directory::_changed(bool set_mtime)
    {
      ELLE_DEBUG("Directory changed: %s with %s entries", this, _files.size());
      elle::Buffer data;
      {
        elle::IOStream os(data.ostreambuf());
        elle::serialization::json::SerializerOut output(os, version);
        output.serialize_forward(*this);
      }
      _block->data(data);
      if (set_mtime && _parent)
      {
        FileData& f = _parent->_files.at(_name);
        f.mtime = time(nullptr);
        f.ctime = time(nullptr);
        f.address = _block->address();
        _parent->_changed();
      }
      _push_changes();
    }

    void
    Directory::_push_changes()
    {
      ELLE_DEBUG("Directory pushChanges: %s on %x size %s",
                 this, _block->address(), _block->data().size());
      _owner.block_store()->store(*_block);
      ELLE_DEBUG("pushChange ok");
    }

    std::shared_ptr<rfs::Path>
    Directory::child(std::string const& name)
    {
      if (!_owner.single_mount())
        _fetch();
      ELLE_DEBUG_SCOPE("Directory child: %s / %s", *this, name);
      auto it = _files.find(name);
      auto self = std::dynamic_pointer_cast<Directory>(shared_from_this());
      ELLE_DEBUG("Acquired self, file found = %s", it != _files.end());
      if (it != _files.end())
      {
        bool isDir = (it->second.mode & S_IFMT)  == DIRECTORY_MASK;
        bool isSymlink = (it->second.mode & S_IFMT) == SYMLINK_MASK;
        ELLE_DEBUG("isDir=%s, isSymlink=%s", isDir, isSymlink);
        if (isSymlink)
          return std::shared_ptr<rfs::Path>(new Symlink(self, _owner, name));
        if (!isDir)
          return std::shared_ptr<rfs::Path>(new File(self, _owner, name));
        std::unique_ptr<MutableBlock> block;
        try
        {
          block = elle::cast<MutableBlock>::runtime
            (_owner.block_store()->fetch(it->second.address));
        }
        catch (infinit::model::MissingBlock const& b)
        {
          throw rfs::Error(EIO, b.what());
        }
        return std::shared_ptr<rfs::Path>(new Directory(self, _owner, name,
                                                        std::move(block)));
      }
      else
        return std::shared_ptr<rfs::Path>(new Unknown(self, _owner, name));
    }

    void
    Directory::list_directory(rfs::OnDirectoryEntry cb)
    {
      if (!_owner.single_mount())
        _fetch();
      ELLE_DEBUG("Directory list: %s", this);
      struct stat st;
      for (auto const& e: _files)
      {
        st.st_mode = e.second.mode;
        st.st_size = e.second.size;
        st.st_atime = e.second.atime;
        st.st_mtime = e.second.mtime;
        st.st_ctime = e.second.ctime;
        cb(e.first, &st);
      }
    }

    void
    Directory::rmdir()
    {
      if (!_files.empty())
        throw rfs::Error(ENOTEMPTY, "Directory not empty");
      if (_parent.get() == nullptr)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      _parent->_files.erase(_name);
      _parent->_changed();
      _owner.block_store()->remove(_block->address());
      _remove_from_cache();
    }

    void
    Directory::move_recurse(boost::filesystem::path const& current,
                            boost::filesystem::path const& where)
    {
      for (auto const& v: _files)
      {
        std::string const& name = v.first;
        ELLE_DEBUG("Extracting %s", current / name);
        auto p = _owner.filesystem()->extract((current / name).string());
        if (p)
        {
          auto ptr = p.get();
          ELLE_DEBUG("Inserting %s", where / name);
          _owner.filesystem()->set((where/name).string(), std::move(p));
          if ((v.second.mode & S_IFMT) ==  DIRECTORY_MASK)
          {
            dynamic_cast<Directory*>(ptr)->move_recurse(current / name, where / name);
          }
        }
      }
    }

    void
    Directory::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      Node::rename(where);
      // We might have children that pointed to us, we need to move them
      this->move_recurse(current, where);
    }

    void
    Node::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      if (!_parent)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_files.find(newname) != dir->_files.end())
      {
        // File and empty dir gets removed.
        auto target = _owner.filesystem()->path(where.string());
        struct stat st;
        target->stat(&st);
        if ((st.st_mode & S_IFMT) == DIRECTORY_MASK)
        {
          try
          {
            target->rmdir();
          }
          catch(rfs::Error const& e)
          {
            throw rfs::Error(EISDIR, "Target is a directory");
          }
        }
        else
          target->unlink();
        ELLE_DEBUG("removed move target %s", where);
      }
      auto data = _parent->_files.at(_name);
      _parent->_files.erase(_name);
      _parent->_changed();
      data.name = newname;
      dir->_files.insert(std::make_pair(newname, data));
      dir->_changed();
      _name = newname;
      _parent = dir;
      // Move the node in cache
      ELLE_DEBUG("Extracting %s", current);
      auto p = _owner.filesystem()->extract(current.string());
      if (p)
      {
        std::dynamic_pointer_cast<Node>(p)->_name = newname;
        // This might delete the dummy Unknown on destination which is fine
        ELLE_DEBUG("Setting %s", where);
        _owner.filesystem()->set(where.string(), p);
      }
    }

    void
    Node::_remove_from_cache()
    {
      ELLE_DEBUG("remove_from_cache: %s entering", _name);
      std::shared_ptr<rfs::Path> self = _owner.filesystem()->extract(full_path().string());
      ELLE_DEBUG("remove_from_cache: %s released", _name);
      new reactor::Thread("delayed_cleanup", [self] { ELLE_DEBUG("async_clean");}, true);
      ELLE_DEBUG("remove_from_cache: %s exiting with async cleaner", _name);
    }

    boost::filesystem::path
    Node::full_path()
    {
      if (_parent == nullptr)
        return "/";
      return _parent->full_path() / _name;
    }

    void
    Directory::stat(struct stat* st)
    {
      ELLE_DEBUG("stat on dir %s", _name);
      Node::stat(st);
    }

    void
    Directory::cache_stats(CacheStats& cs)
    {
      cs.directories++;
      boost::filesystem::path current = full_path();
      for(auto const& f: _files)
      {
        auto p = _owner.filesystem()->get((current / f.second.name).string());
        if (!p)
          return;
        if (Directory* d = dynamic_cast<Directory*>(p.get()))
          d->cache_stats(cs);
        else if (File* f = dynamic_cast<File*>(p.get()))
        {
          cs.files++;
          cs.blocks += 1 + f->_blocks.size();
          if (f->_first_block)
            cs.size += f->_first_block->data().size();
          for (auto& b: f->_blocks)
            cs.size += b.second.block.data().size();
        }
      }
    }

    void
    Directory::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    File::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    Node::chmod(mode_t mode)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.mode = (f.mode & ~07777) | (mode & 07777);
      f.ctime = time(nullptr);
      _parent->_changed();
    }

    void
    Directory::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void
    File::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void
    Node::chown(int uid, int gid)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.uid = uid;
      f.gid = gid;
      f.ctime = time(nullptr);
      _parent->_changed();
    }

    void
    Node::stat(struct stat* st)
    {
      memset(st, 0, sizeof(struct stat));
      if (_parent)
      {
        auto fd = _parent->_files.at(_name);
        st->st_blksize = 16384;
        st->st_mode = fd.mode;
        st->st_size = fd.size;
        st->st_atime = fd.atime;
        st->st_mtime = fd.mtime;
        st->st_ctime = fd.ctime;
        st->st_dev = 1;
        st->st_ino = (long)this;
        st->st_nlink = 1;
      }
      else
      { // Root directory permissions
        st->st_mode = DIRECTORY_MASK | 0777;
        st->st_size = 0;
      }
      st->st_uid = ::getuid();
      st->st_gid = ::getgid();
    }

    Unknown::Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name)
      : Node(owner, parent, name)
    {}

    void
    Unknown::mkdir(mode_t mode)
    {
      ELLE_DEBUG("mkdir %s", _name);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      _owner.block_store()->store(*b, model::STORE_INSERT);
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      _parent->_files.insert(
        std::make_pair(_name,
                       FileData{_name, 0, mode | DIRECTORY_MASK,
                                uint64_t(time(nullptr)),
                                uint64_t(time(nullptr)),
                                uint64_t(time(nullptr)),
                                b->address(),
                                FileStoreMode::direct}));
      _parent->_changed();
      _remove_from_cache();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      ELLE_ASSERT_EQ(signed(mode & S_IFMT), S_IFREG);
      if (!_owner.single_mount())
        _parent->_fetch();
      if (_parent->_files.find(_name) != _parent->_files.end())
      {
        ELLE_WARN("File %s exists where it should not", _name);
        _remove_from_cache();
        auto f = std::dynamic_pointer_cast<File>(_owner.filesystem()->path(full_path().string()));
        return f->open(flags, mode);
      }
      auto b = _owner.block_store()->make_block<MutableBlock>();
      //optimize: dont push block yet _owner.block_store()->store(*b);
      ELLE_DEBUG("Adding file to parent %x", _parent.get());
      _parent->_files.insert(
        std::make_pair(_name, FileData{_name, 0, mode,
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       b->address(),
                                       FileStoreMode::direct}));
      try
      {
        _parent->_changed(true);
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("Error updating directory at %s: %s", full_path(), e.what());
        THROW_ACCES;
      }
      _remove_from_cache();
      auto raw = _owner.filesystem()->path(full_path().string());
      auto f = std::dynamic_pointer_cast<File>(raw);
      if (!f)
        ELLE_ERR("Expected valid pointer from %s, got nullptr", raw.get());
      f->_first_block = std::move(b);
      f->_first_block_new = true;
      // Mark dirty since we did not push first_block
      ELLE_DEBUG("Forcing entry %s", f->full_path());
      _owner.filesystem()->set(f->full_path().string(), f);
      std::unique_ptr<rfs::Handle> h(new FileHandle(f, true, true, true));
      return h;
    }

    void
    Unknown::symlink(boost::filesystem::path const& where)
    {
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      Address::Value v {0};
      auto it =_parent->_files.insert(
        std::make_pair(_name, FileData{_name, 0, 0777 | SYMLINK_MASK,
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       Address(v),
                                       FileStoreMode::none}));
      it.first->second.symlink_target = where.string();
      _parent->_changed(true);
      _remove_from_cache();
    }

    void
    Unknown::link(boost::filesystem::path const& where)
    {
      throw rfs::Error(ENOENT, "link source does not exist");
    }

    Symlink::Symlink(DirectoryPtr parent,
                     FileSystem& owner,
                     std::string const& name)
      : Node(owner, parent, name)
    {}

    void
    Symlink::stat(struct stat* s)
    {
      Node::stat(s);
    }

    void
    Symlink::unlink()
    {
      _parent->_files.erase(_name);
      _parent->_changed(true);
      _remove_from_cache();
    }

    void
    Symlink::rename(boost::filesystem::path const& where)
    {
      Node::rename(where);
    }

    boost::filesystem::path
    Symlink::readlink()
    {
      return *_parent->_files.at(_name).symlink_target;
    }

    void
    Symlink::link(boost::filesystem::path const& where)
    {
      auto p = _owner.filesystem()->path(where.string());
      Unknown* unk = dynamic_cast<Unknown*>(p.get());
      if (unk == nullptr)
        THROW_EXIST;
      unk->symlink(readlink());
    }

    File::File(DirectoryPtr parent, FileSystem& owner, std::string const& name,
               std::unique_ptr<MutableBlock> block)
    : Node(owner, parent, name)
    , _first_block(std::move(block))
    , _first_block_new(false)
    , _handle_count(0)
    {}

    bool
    File::allow_cache()
    {
      return _owner.single_mount() ||  _handle_count > 0;
    }

    void
    File::statfs(struct statvfs * st)
    {
      memset(st, 0, sizeof(struct statvfs));
      st->f_bsize = 32768;
      st->f_frsize = 32768;
      st->f_blocks = 1000000;
      st->f_bavail = 1000000;
      st->f_fsid = 1;
    }
    bool
    File::_multi()
    {
      ELLE_DEBUG("Multi check %s", _name);
      ELLE_ASSERT(!!_parent);
      ELLE_ASSERT(_parent->_files.find(_name) != _parent->_files.end());
      return _parent->_files.at(_name).store_mode == FileStoreMode::index;
    }

    File::Header
    File::_header()
    {
      Header res;
      uint32_t v;
      memcpy(&v, _first_block->data().mutable_contents(), 4);
      res.version = ntohl(v);
      memcpy(&v, _first_block->data().mutable_contents()+4, 4);
      res.block_size = ntohl(v);
      memcpy(&v, _first_block->data().mutable_contents()+8, 4);
      res.links = ntohl(v);
      uint64_t v2;
      memcpy(&v2, _first_block->data().mutable_contents()+12, 8);
      res.total_size = ((uint64_t)ntohl(v2)<<32) + ntohl(v2 >> 32);
      return res;
      ELLE_DEBUG("Header: bs=%s links=%s size=%s", res.block_size, res.links, res.total_size);
    }

    void
    File::_header(Header const& h)
    {
      uint32_t v;
      v = htonl(h.current_version);
      memcpy(_first_block->data().mutable_contents(), &v, 4);
      v = htonl(h.block_size);
      memcpy(_first_block->data().mutable_contents()+4, &v, 4);
      v = htonl(h.links);
      memcpy(_first_block->data().mutable_contents()+8, &v, 4);
      uint64_t v2 = ((uint64_t)htonl(h.total_size)<<32) + htonl(h.total_size >> 32);
      memcpy(_first_block->data().mutable_contents()+12, &v2, 8);
    }

    AnyBlock*
    File::_block_at(int index, bool create)
    {
      ELLE_ASSERT_GTE(index, 0);
      int offset = (index+1) * sizeof(Address);
      int sz = _first_block->data().size();
      if (sz < offset + signed(sizeof(Address)))
      {
        if (!create)
        {
          return nullptr;
        }
        this->_first_block->data(
          [offset, sz] (elle::Buffer& data)
          {
            data.size(offset + sizeof(Address));
            memset
              (data.mutable_contents() + sz, 0, offset + sizeof(Address) - sz);
          });
      }
      char zeros[sizeof(Address)];
      memset(zeros, 0, sizeof(Address));
      AnyBlock b;
      bool is_new = false;
      if (!memcmp(zeros, _first_block->data().mutable_contents() + offset,
                 sizeof(Address)))
      { // allocate
        if (!create)
        {
          return nullptr;
        }
        b = AnyBlock(_owner.block_store()->make_block<ImmutableBlock>());
        is_new = true;
        // _owner.block_store()->store(*b); // FIXME: but why?
      }
      else
      {
         Address addr = Address(*(Address*)(_first_block->data().mutable_contents() + offset));
         b = AnyBlock(_owner.block_store()->fetch(addr));
      }
      _first_block->data([&](elle::Buffer& data)
        {
          memcpy(data.mutable_contents() + offset,
            b.address().value(), sizeof(Address::Value));
        });
      _owner.block_store()->store(*_first_block);
      auto inserted = _blocks.insert(std::make_pair(index,
        File::CacheEntry{AnyBlock(std::move(b)), false}));
      inserted.first->second.last_use = std::chrono::system_clock::now();
      inserted.first->second.dirty = true;
      inserted.first->second.new_block = is_new;
      return &inserted.first->second.block;
    }

    void
    File::link(boost::filesystem::path const& where)
    {
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_files.find(newname) != dir->_files.end())
        throw rfs::Error(EEXIST, "target file exists");
      // we need a place to store the link count
      bool multi = _multi();
      if (!multi)
        _switch_to_multi(false);
      Header header = _header();
      header.links++;
      _header(header);
      dir->_files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir->_files.at(newname).name = newname;
      dir->_changed(true);
      _owner.filesystem()->extract(where.string());
      _owner.block_store()->store(*_first_block);
    }

    void
    File::unlink()
    {
      static bool no_unlink = !elle::os::getenv("INHIBIT_UNLINK", "").empty();
      if (no_unlink)
      { // DEBUG: link the file in root directory
        std::string n("__" + full_path().string());
        for (unsigned int i=0; i<n.length(); ++i)
          if (n[i] == '/')
          n[i] = '_';
        DirectoryPtr dir = _parent;
        while (dir->_parent)
          dir = dir->_parent;
        auto& cur = _parent->_files.at(_name);
        dir->_files.insert(std::make_pair(n, cur));
        dir->_files.at(n).name = n;
      }
      // multi method can't be called after deletion from parent
      bool multi = _multi();
      Address addr = _parent->_files.at(_name).address;
      _parent->_files.erase(_name);
      _parent->_changed(true);
      if (!multi)
      {
        if (!no_unlink)
          _owner.unchecked_remove(addr);
      }
      else
      {
        _first_block = _owner.unchecked_fetch(addr);
        if (!_first_block)
        {
          _remove_from_cache();
          return;
        }
        int links = _header().links;
        if (links > 1)
        {
          ELLE_DEBUG("%s remaining links", links - 1);
          Header header = _header();
          header.links--;
          _header(header);
          _owner.block_store()->store(*_first_block);
        }
        else
        {
          ELLE_DEBUG("No remaining links");
          Address::Value zero;
          memset(&zero, 0, sizeof(Address::Value));
          Address* addr = (Address*)(void*)_first_block->data().mutable_contents();
          for (unsigned i=1; i*sizeof(Address) < _first_block->data().size(); ++i)
          {
            if (!memcmp(addr[i].value(), zero, sizeof(Address::Value)))
              continue; // unallocated block
            _owner.block_store()->remove(addr[i]);
          }
          _owner.block_store()->remove(_first_block->address());
        }
      }
      _remove_from_cache();
    }

    void
    File::rename(boost::filesystem::path const& where)
    {
      Node::rename(where);
    }

    void
    File::stat(struct stat* st)
    {
      ELLE_DEBUG("stat on file %s", _name);
      Node::stat(st);
      if (_multi())
      {
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.block_store()->fetch(_parent->_files.at(_name).address));
        Header header = _header();
        st->st_size = header.total_size;
        st->st_nlink = header.links;
        ELLE_DEBUG("stat on multi: %s", header.total_size);
      }
      else
      {
        st->st_size = _parent->_files.at(_name).size;
        st->st_nlink = 1;
        ELLE_DEBUG("stat on single: %s", st->st_size);
      }
    }

    void
    File::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    Directory::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    Node::utimens(const struct timespec tv[2])
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.atime = tv[0].tv_sec;
      f.mtime = tv[1].tv_sec;
      _parent->_changed();
    }

    void
    File::truncate(off_t new_size)
    {
      if (!_multi() && new_size > signed(default_block_size))
        _switch_to_multi(true);
      if (!_multi())
      {
        _first_block->data
          ([new_size] (elle::Buffer& data) { data.size(new_size); });
      }
      else
      {
        Header header = _header();
        if (header.total_size <= unsigned(new_size))
        {
          header.total_size = new_size;
          _header(header);
          _owner.block_store()->store(*_first_block);
          return;
        }
        // FIXME: addr should be a Address::Value
        uint32_t block_size = header.block_size;
        Address::Value zero;
        memset(&zero, 0, sizeof(Address::Value));
        Address* addr = (Address*)(void*)_first_block->data().mutable_contents();
        // last block id to keep, always keep block 0
        int drop_from = new_size? (new_size-1) / block_size : 0;
        // addr[0] is our headers
        for (unsigned i=drop_from + 2; i*sizeof(Address) < _first_block->data().size(); ++i)
        {
           if (!memcmp(addr[i].value(), zero, sizeof(Address::Value)))
            continue; // unallocated block
          _owner.block_store()->remove(addr[i]);
          _blocks.erase(i-1);
        }
        _first_block->data(
          [drop_from] (elle::Buffer& data)
          {
            data.size((drop_from + 2) * sizeof(Address));
          });
        // last block surviving the cut might need resizing
        if (drop_from >=0 && !memcmp(addr[drop_from+1].value(), zero, sizeof(Address::Value)))
        {
          AnyBlock* bl;
          auto it = _blocks.find(drop_from);
          if (it != _blocks.end())
          {
            bl = &it->second.block;
            it->second.dirty = true;
          }
          else
          {
            _blocks[drop_from].block =
              AnyBlock(_owner.block_store()->fetch(addr[drop_from + 1]));
            CacheEntry& ent = _blocks[drop_from];
            bl = &ent.block;
            ent.dirty = true;
          }
          bl->data([new_size, block_size] (elle::Buffer& data)
                   { data.size(new_size % block_size); });
        }
        header.total_size = new_size;
        _header(header);
        if (new_size <= block_size && header.links == 1)
        { // switch back from multi to direct
          auto& data = _parent->_files.at(_name);
          data.store_mode = FileStoreMode::direct;
          data.size = new_size;
          _parent->_changed();
          // Replacing FAT block with first block would be simpler,
          // but it might be immutable
          AnyBlock* data_block;
          auto it = _blocks.find(0);
          if (it != _blocks.end())
            data_block = & it->second.block;
          else
          {
            std::unique_ptr<Block> bl = _owner.block_store()->fetch(addr[1]);
            _blocks[0] = {AnyBlock(std::move(bl)), false, {}, false};
            data_block = &_blocks[0].block;
          }
          _owner.block_store()->remove(addr[1]);
          _first_block->data
            ([&] (elle::Buffer& data) {
              data.size(0);
              data.append(data_block->data().contents(), new_size);
            });
          _blocks.clear();
          _owner.block_store()->store(*_first_block, infinit::model::STORE_UPDATE);
        }
      }
      _changed();
    }

    std::unique_ptr<rfs::Handle>
    File::open(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      ELLE_DEBUG("Forcing entry %s", full_path());
      _owner.filesystem()->set(full_path().string(), shared_from_this());
      try {
        return std::unique_ptr<rfs::Handle>(new FileHandle(
          std::dynamic_pointer_cast<File>(shared_from_this())));
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("Error opening file %s: %s", full_path(), e.what());
        THROW_ACCES;
      }
    }

    std::unique_ptr<rfs::Handle>
    File::create(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      ELLE_DEBUG("Forcing entry %s", full_path());
      if (!_owner.single_mount())
        _owner.filesystem()->set(full_path().string(), shared_from_this());
      return std::unique_ptr<rfs::Handle>(new FileHandle(
        std::dynamic_pointer_cast<File>(shared_from_this())));
    }

    void
    File::_changed()
    {
      auto& data = _parent->_files.at(_name);
      data.mtime = time(nullptr);
      data.ctime = time(nullptr);
      if (!_multi())
        data.size = _first_block->data().size();
      else
      {
        std::unordered_map<int, CacheEntry> blocks;
        std::swap(blocks, _blocks);
        for (auto& b: blocks)
        { // FIXME: incremental size compute
          ELLE_DEBUG("Checking data block %s :%x, size %s",
            b.first, b.second.block.address(), b.second.block.data().size());
          if (b.second.dirty)
          {
            ELLE_DEBUG("Writing data block %s", b.first);
            b.second.dirty = false;
            Address prev = b.second.block.address();
            Address addr = b.second.block.store(*_owner.block_store(), b.second.new_block? model::STORE_INSERT : model::STORE_ANY);
            if (addr != prev)
            {
              ELLE_DEBUG("Changing address of block %s: %s -> %s", b.first,
                prev, addr);
              int offset = (b.first+1) * sizeof(Address);
              _first_block->data([&] (elle::Buffer& data)
                {
                  memcpy(data.contents() + offset, addr.value(), sizeof(Address::Value));
                });
              if (!b.second.new_block)
              {
                _owner.unchecked_remove(prev);
              }
              b.second.new_block = true;
            }
            b.second.new_block = false;
            b.second.dirty = false;
          }
        }
      }
      ELLE_DEBUG("Storing first block %x, size %s",
                 _first_block->address(), _first_block->data().size());
      _owner.block_store()->store(*_first_block,
                                  _first_block_new ? model::STORE_INSERT : model::STORE_ANY);
      _first_block_new = false;
      _parent->_changed(false);
    }

    void
    File::_switch_to_multi(bool alloc_first_block)
    {
      // Switch without changing our address
      if (!_first_block)
      _first_block = elle::cast<MutableBlock>::runtime
        (_owner.block_store()->fetch(_parent->_files.at(_name).address));
      uint64_t current_size = _first_block->data().size();
      auto new_block = _owner.block_store()->make_block<ImmutableBlock>(
        _first_block->data());
      ELLE_ASSERT_EQ(current_size, new_block->data().size());
      _blocks.insert(std::make_pair(0, CacheEntry{
        AnyBlock(std::move(new_block)), true, {}, true}));
      ELLE_ASSERT_EQ(current_size, _blocks.at(0).block.data().size());
      /*
      // current first_block becomes block[0], first_block becomes the index
      _blocks[0] = std::move(_first_block);
      _first_block = _owner.block_store()->make_block<Block>();
      _parent->_files.at(_name).address = _first_block->address();
      _parent->_changed();
      */
      _first_block->data
        ([] (elle::Buffer& data) { data.size(sizeof(Address)* 2); });
      // store block size in headers
      Header h { Header::current_version, default_block_size, 1, current_size};
      _header(h);
      _first_block->data([&](elle::Buffer& data)
        {
          memcpy(data.mutable_contents() + sizeof(Address),
            _blocks.at(0).block.address().value(), sizeof(Address::Value));
        });
      _parent->_files.at(_name).store_mode = FileStoreMode::index;
      _parent->_changed();
      // we know the operation that triggered us is going to expand data
      // beyond first block, so it is safe to resize here
      if (alloc_first_block)
      {
        auto& b = _blocks.at(0);
        int64_t old_size = b.block.data().size();
        b.block.data([] (elle::Buffer& data) {data.size(default_block_size);});
        if (old_size != default_block_size)
          b.block.zero(old_size, default_block_size - old_size);
      }
      _changed();
    }

    void
    File::check_cache()
    {
      typedef std::pair<const int, CacheEntry> Elem;
      bool fat_change = false;
      while (_blocks.size() > max_cache_size)
      {
        auto it = std::min_element(_blocks.begin(), _blocks.end(),
          [](Elem const& a, Elem const& b) -> bool
          {
            return a.second.last_use < b.second.last_use;
          });
        ELLE_DEBUG("Removing block %s from cache", it->first);
        if (it->second.dirty)
        {
          Address prev = it->second.block.address();
          Address addr = it->second.block.store(*_owner.block_store(),
                                                it->second.new_block? model::STORE_INSERT : model::STORE_ANY);
          it->second.new_block = false;
          if (addr != prev)
          {
            ELLE_DEBUG("Changing address of block %s: %s -> %s", it->first,
              prev, addr);
            fat_change = true;
            int offset = (it->first+1) * sizeof(Address);
            _first_block->data([&] (elle::Buffer& data)
              {
                memcpy(data.contents() + offset, addr.value(), sizeof(Address::Value));
              });
            if (!it->second.new_block)
            {
              _owner.unchecked_remove(prev);
            }
            it->second.new_block = true;
          }
        }
        _blocks.erase(it);
      }
      if (fat_change)
      {
        _owner.block_store()->store(*_first_block,
                                  _first_block_new ? model::STORE_INSERT : model::STORE_ANY);
        _first_block_new = false;
      }
    }

    std::vector<std::string> File::listxattr()
    {
      ELLE_TRACE("listxattr");
      std::vector<std::string> res;
      res.push_back("user.infinit.block");
      res.push_back("user.infinit.auth.setr");
      res.push_back("user.infinit.auth.setrw");
      res.push_back("user.infinit.auth.setw");
      res.push_back("user.infinit.auth.clear");
      return res;
    }
    std::vector<std::string> Directory::listxattr()
    {
      ELLE_TRACE("listxattr");
      std::vector<std::string> res;
      res.push_back("user.infinit.block");
      return res;
    }
    std::string File::getxattr(std::string const& key)
    {
      ELLE_TRACE("getxattr %s", key);
      if (key == "user.infinit.block")
      {
        if (_first_block)
          return elle::sprintf("%x", _first_block->address());
        else
        {
          auto const& elem = _parent->_files.at(_name);
          return elle::sprintf("%x", elem.address);
        }
      }
      else
        THROW_NODATA;
    }
    void File::setxattr(std::string const& name, std::string const& value, int flags)
    {
      THROW_INVAL;
    }
    void Directory::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("setxattr %s", name);
      if (name.find("user.infinit.auth.") == 0)
      {
        std::string flags = name.substr(strlen("user.infinit.auth."));
        bool r = false;
        bool w = false;
        if (flags == "clear")
          ;
        else if (flags == "setr")
          r = true;
        else if (flags == "setw")
          w = true;
        else if (flags == "setrw")
        {
          r = true; w = true;
        }
        else
          THROW_NODATA;
        if (value.empty())
          THROW_INVAL;
        if (value.find_first_of("/\\. ") != value.npos)
          THROW_INVAL;
        elle::Buffer userkey;
        if (value[0] == '$')
        { // user name, fetch the key
          std::shared_ptr<Path> p = _owner.path("/");
          auto users = dynamic_cast<Directory*>(p.get())->child("$users");
          auto user = dynamic_cast<Directory*>(users.get())->child(value.substr(1));
          File* f = dynamic_cast<File*>(user.get());
          ELLE_TRACE("user by name failed: %s %s", user.get(), f);
          if (!f)
            THROW_INVAL;
          std::unique_ptr<rfs::Handle> h = f->open(0, O_RDONLY);
          userkey.size(16384);
          int len = h->read(userkey, 16384, 0);
          userkey.size(len);
          h->close();
        }
        else
        { // user key
          ELLE_TRACE("setxattr raw key");
          userkey = elle::Buffer(value.data(), value.size());
        }
        auto user = _owner.block_store()->make_user(userkey);
        model::blocks::ACLBlock* acl
          = dynamic_cast<model::blocks::ACLBlock*>(_block.get());
        if (acl == 0)
        { // Damm
          ELLE_TRACE("Not an acl block, updating");
          std::unique_ptr<model::blocks::ACLBlock> nacl
            = _owner.block_store()->make_block<model::blocks::ACLBlock>();
          _owner.block_store()->remove(_block->address());
          _block = std::move(nacl);
          _changed(true);
          acl = dynamic_cast<model::blocks::ACLBlock*>(_block.get());
        }
        ELLE_TRACE("Setting permission at %s", acl->address());
        acl->set_permissions(*user, r, w);
        _owner.block_store()->store(*acl); // FIXME STORE MODE
      }
      else
        THROW_NODATA;
    }

    std::string Directory::getxattr(std::string const& key)
    {
      ELLE_TRACE("getxattr %s", key);
      if (key == "user.infinit.block")
      {
        if (_block)
          return elle::sprintf("%x", _block->address());
        else if (_parent)
        {
          auto const& elem = _parent->_files.at(_name);
          return elle::sprintf("%x", elem.address);
        }
        else
          return "<ROOT>";
      }
      else
        THROW_NODATA;
    }
    FileHandle::FileHandle(std::shared_ptr<File> owner,
                           bool push_mtime,
                           bool no_fetch,
                           bool dirty)
      : _owner(owner)
      , _dirty(dirty)
    {
      ELLE_TRACE("FileHandle creation, hc=%s", _owner->_handle_count);
      _owner->_handle_count++;
      _owner->_parent->_fetch();
      _owner->_parent->_files.at(_owner->_name).atime = time(nullptr);
      try
      {
        _owner->_parent->_changed(push_mtime);
      }
      catch (std::exception const& e)
      {
        ELLE_TRACE("Error writing atime %s: %s", _owner->full_path(), e.what());
      }
      // FIXME: the only thing that can invalidate _owner is hard links
      // keep tracks of open handle to know if we should refetch
      // or a backend stat call?
      if (!no_fetch)
      {
        try
        {
          auto address = _owner->_parent->_files.at(_owner->_name).address;
          _owner->_first_block = elle::cast<MutableBlock>::runtime
            (_owner->_owner.block_store()->fetch(address));
        }
        catch(infinit::model::MissingBlock const& err)
        {
          // This is not a mistake if file is already opened but data has not
          // been pushed yet.
          if (!_owner->_first_block)
          {
            ELLE_ERR("Block missing in storage and not in cache.");
            throw;
          }
        }
        catch(std::exception const& e)
        {
          ELLE_WARN("Unexpected exception while fetching: %s", e.what());
          throw;
        }
      }
    }

    FileHandle::~FileHandle()
    {
      _owner->_handle_count--;
      close();
    }

    void
    FileHandle::close()
    {
      ELLE_DEBUG("Closing %s with dirty=%s", _owner->_name, _dirty);
      if (_dirty)
        _owner->_changed();
      _dirty = false;
      _owner->_blocks.clear();
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_DEBUG("read %s at %s", size, offset);
      ELLE_ASSERT_EQ(buffer.size(), size);
      int64_t total_size;
      int32_t block_size;
      if (_owner->_multi())
      {
        File::Header h = _owner->_header();
        total_size = h.total_size;
        block_size = h.block_size;
      }
      else
      {
        total_size = _owner->_parent->_files.at(_owner->_name).size;
        block_size = _owner->default_block_size;
      }
      if (offset >= total_size)
      {
        ELLE_DEBUG("read past end: offset=%s, size=%s", offset, total_size);
        return 0;
      }
      if (signed(offset + size) > total_size)
      {
        ELLE_DEBUG("read past end, reducing size from %s to %s", size,
                   total_size - offset);
        size = total_size - offset;
      }
      if (!_owner->_multi())
      { // single block case
        auto& block = _owner->_first_block;
        if (!block)
        {
          ELLE_DEBUG("read on uncached block, fetching");
          auto address = _owner->_parent->_files.at(_owner->_name).address;
          _owner->_first_block = elle::cast<MutableBlock>::runtime
            (_owner->_owner.block_store()->fetch(address));
        }
        ELLE_ASSERT_EQ(signed(block->data().size()), total_size);
        memcpy(buffer.mutable_contents(),
               block->data().mutable_contents() + offset,
               size);
        ELLE_DEBUG("read %s bytes", size);
        return size;
      }
      // multi case
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      { // single block case
        off_t block_offset = offset - (off_t)start_block * (off_t)block_size;
        auto const& it = _owner->_blocks.find(start_block);
        AnyBlock* block = nullptr;
        if (it != _owner->_blocks.end())
        {
          ELLE_DEBUG("obtained block %s : %x from cache", start_block, it->second.block.address());
          block = &it->second.block;
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          block = _owner->_block_at(start_block, false);
          if (block == nullptr)
          { // block would have been allocated: sparse file?
            memset(buffer.mutable_contents(), 0, size);
            ELLE_DEBUG("read %s 0-bytes", size);
            return size;
          }
          ELLE_DEBUG("fetched block %x of size %s", block->address(), block->data().size());
          _owner->check_cache();
        }
        ELLE_ASSERT_LTE(signed(block_offset + size), block_size);
        if (block->data().size() < block_offset + size)
        { // sparse file, eof shrinkage of size was handled above
          long available = block->data().size() - block_offset;
          if (available < 0)
            available = 0;
          ELLE_DEBUG("no data for %s out of %s bytes",
                     size - available, size);
          if (available)
            memcpy(buffer.mutable_contents(),
                   block->data().contents() + block_offset,
                   available);
          memset(buffer.mutable_contents() + available, 0, size - available);
        }
        else
        {
          block->data(
            [&buffer, block_offset, size] (elle::Buffer& data)
            {
              memcpy(buffer.mutable_contents(), &data[block_offset], size);
            });
          ELLE_DEBUG("read %s bytes", size);
        }
        return size;
      }
      else
      { // overlaps two blocks case
        ELLE_ASSERT(start_block == end_block - 1);
        int64_t second_size = (offset + size) % block_size; // second block
        int64_t first_size = size - second_size;
        int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
        ELLE_DEBUG("split %s %s into %s %s and %s %s",
                   size, offset, first_size, offset, second_size, second_offset);
        int r1 = read(elle::WeakBuffer(buffer.mutable_contents(), first_size),
                      first_size, offset);
        if (r1 <= 0)
          return r1;
        int r2 = read(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                 second_size, second_offset);
        if (r2 < 0)
          return r2;
        ELLE_DEBUG("read %s+%s=%s bytes", r1, r2, r1+r2);
        return r1 + r2;
      }
    }

    int
    FileHandle::write(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_ASSERT_EQ(buffer.size(), size);
      ELLE_DEBUG("write %s at %s on %s", size, offset, _owner->_name);
      if (!_owner->_multi() && size + offset > _owner->default_block_size)
        _owner->_switch_to_multi(true);
      _dirty = true;
      if (!_owner->_multi())
      {
        auto& block = _owner->_first_block;
        if (offset + size > block->data().size())
        {
          int64_t old_size = block->data().size();
          block->data ([&] (elle::Buffer& data){
              data.size(offset + size);
              if (old_size < offset)
                memset(data.mutable_contents() + old_size, 0, offset - old_size);
            });
        }
        block->data ([&] (elle::Buffer& data){
            memcpy(data.mutable_contents() + offset, buffer.contents(), size);
        });
        // Update but do not commit yet, so that read on same fd do not fail.
        FileData& data = _owner->_parent->_files.at(_owner->_name);
        if (data.size < offset + size)
        {
          data.size = offset + size;
          data.mtime = time(nullptr);
          data.ctime = time(nullptr);
        }
        return size;
      }
      // multi mode
      uint64_t block_size = _owner->_header().block_size;
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      {
        AnyBlock* block;
        auto const it = _owner->_blocks.find(start_block);
        if (it != _owner->_blocks.end())
        {
          block = &it->second.block;
          it->second.dirty = true;
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          block = _owner->_block_at(start_block, true);
          ELLE_ASSERT(block != nullptr);
          _owner->check_cache();
        }
        off_t block_offset = offset % block_size;
        bool growth = false;
        if (block->data().size() < block_offset + size)
        {
          growth = true;
          int64_t old_size = block->data().size();
          block->data(
            [block_offset, size] (elle::Buffer& data)
            {
              data.size(block_offset + size);
            });
          ELLE_DEBUG("Growing block of %s to %s", block_offset + size - old_size,
            block_offset + size);
          if (old_size < block_offset)
          { // fill with zeroes
            block->zero(old_size, block_offset - old_size);
          }
        }
        block->write(block_offset, buffer.contents(), size);
        if (growth)
        { // check if file size was increased
          File::Header h = _owner->_header();
          if (h.total_size < offset + size)
          {
            h.total_size = offset + size;
            ELLE_DEBUG("New file size: %s", h.total_size);
            _owner->_header(h);
          }
        }
        return size;
      }
      // write across blocks
      ELLE_ASSERT(start_block == end_block - 1);
      int64_t second_size = (offset + size) % block_size; // second block
      int64_t first_size = size - second_size;
      int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
      int r1 = write(elle::WeakBuffer(buffer.mutable_contents(), first_size),
                    first_size, offset);
      if (r1 <= 0)
        return r1;
      int r2 = write(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                    second_size, second_offset);
      if (r2 < 0)
        return r2;
      // Assuming linear writes, this is a good time to flush start block since
      // it just got filled
      File::CacheEntry& ent = _owner->_blocks.at(start_block);
      Address prev = ent.block.address();
      Address cur = ent.block.store(*_owner->_owner.block_store(),
        ent.new_block? model::STORE_INSERT : model::STORE_ANY);
      if (cur != prev)
      {
        ELLE_DEBUG("Changing address of block %s: %s -> %s", start_block,
                   prev, cur);
        int offset = (start_block+1) * sizeof(Address);
        _owner->_first_block->data([&](elle::Buffer& data)
          {
            memcpy(data.mutable_contents() + offset, cur.value(), sizeof(Address::Value));
          });
        if (!ent.new_block)
          _owner->_owner.block_store()->remove(prev);
      }

      ent.dirty = false;
      ent.new_block = false;
      _owner->_owner.block_store()->store(*_owner->_first_block,
                                  _owner->_first_block_new ? model::STORE_INSERT : model::STORE_ANY);
      _owner->_first_block_new = false;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t offset)
    {
      return _owner->truncate(offset);
    }
  }
}
