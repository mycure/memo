#pragma once

#include <infinit/filesystem/Node.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/filesystem/umbrella.hh>
#include <reactor/filesystem.hh>

namespace infinit
{
  namespace filesystem
  {
    typedef std::shared_ptr<Directory> DirectoryPtr;

    class Symlink
      : public Node
      , public rfs::Path
    {
    public:
      Symlink(FileSystem& owner,
              Address address,
              std::shared_ptr<DirectoryData> parent,
              std::string const& name);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override { THROW_NOTDIR(); }
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override { THROW_NOSYS(); }
      void unlink() override;
      void mkdir(mode_t mode) override { THROW_EXIST(); }
      void rmdir() override { THROW_NOTDIR(); }
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override;
      void symlink(boost::filesystem::path const& where) override { THROW_EXIST(); }
      void link(boost::filesystem::path const& where) override; //copied symlink
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override { THROW_NOSYS(); }
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override { THROW_NOSYS(); }
      std::shared_ptr<Path> child(std::string const& name) override { THROW_NOTDIR(); }
      std::string getxattr(std::string const& key) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void removexattr(std::string const& name) override;
      bool allow_cache() override { return false;}
      void _fetch() override;
      void _commit(WriteTarget target) override;
      model::blocks::ACLBlock* _header_block(bool) override;
      FileHeader& _header() override;
      virtual void print(std::ostream& stream) const override;
      std::unique_ptr<MutableBlock> _block;
      FileHeader _h;
    };
  }
}
