#include <sys/types.h>

#ifdef INFINIT_LINUX
# include <attr/xattr.h>
#elif defined(INFINIT_MACOSX)
# include <sys/xattr.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <elle/Exit.hh>
#include <elle/Error.hh>
#include <elle/log.hh>
#include <elle/json/json.hh>

ELLE_LOG_COMPONENT("infinit-acl");

#include <main.hh>

#ifdef INFINIT_MACOSX
# define SXA_EXTRA ,0
#else
# define SXA_EXTRA
#endif

infinit::Infinit ifnt;

static const char admin_prefix = '^';
static const char group_prefix = '@';

static
bool
is_admin(std::string const& obj)
{
  return obj.length() > 0 && obj[0] == admin_prefix;
}

static
bool
is_group(std::string const& obj)
{
  return obj.length() > 0 && obj[0] == group_prefix;
}

static
std::string
public_key_from_username(std::string const& username)
{
  auto user = ifnt.user_get(username);
  elle::Buffer buf;
  {
    elle::IOStream ios(buf.ostreambuf());
    elle::serialization::json::SerializerOut so(ios, false);
    so.serialize_forward(user.public_key);
  }
  return buf.string();
}

static
boost::filesystem::path
file_xattrs_dir(std::string const& file)
{
  boost::filesystem::path p(file);
  auto filename = p.filename();
  auto dir = p.parent_path();
  boost::filesystem::path res;
  // dir might be outside the filesystem, so dont go below file if its a
  // directory
  if (boost::filesystem::is_directory(file))
    res = p / "$xattrs..";
  else
    res = dir / ("$xattrs." + filename.string());
  boost::system::error_code erc;
  boost::filesystem::create_directory(res, erc);
  return res;
}

static
int
port_getxattr(std::string const& file,
              std::string const& key,
              char* val, int val_size,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
  int res = -1;
  res = getxattr(file.c_str(), key.c_str(), val, val_size SXA_EXTRA SXA_EXTRA);
  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ifstream ifs(attr_dir / key);
  ifs.read(val, val_size);
  return ifs.gcount();
}

static
int
port_setxattr(std::string const& file,
              std::string const& key,
              std::string const& value,
              bool fallback_xattrs)
{
#ifndef INFINIT_WINDOWS
  int res = setxattr(
    file.c_str(), key.c_str(), value.data(), value.size(), 0 SXA_EXTRA);
  if (res >= 0 || !fallback_xattrs)
    return res;
#endif
  if (!fallback_xattrs)
    elle::unreachable();
  auto attr_dir = file_xattrs_dir(file);
  boost::filesystem::ofstream ofs(attr_dir / key);
  ofs.write(value.data(), value.size());
  return 0;
}

class InvalidArgument
  : public elle::Error
{
public:
  InvalidArgument(std::string const& error)
    : elle::Error(error)
  {}
};

template<typename F, typename ... Args>
void
check(F func, Args ... args)
{
  int res = func(args...);
  if (res < 0)
  {
    int error_number = errno;
    auto* e = std::strerror(error_number);
    if (error_number == EINVAL)
      throw InvalidArgument(std::string(e));
    else
      throw elle::Error(std::string(e));
  }
}

template<typename A, typename ... Args>
void
recursive_action(A action, std::string const& path, Args ... args)
{
  namespace bfs = boost::filesystem;
  boost::system::error_code erc;
  bfs::recursive_directory_iterator it(path, erc);
  if (erc)
    throw elle::Error(elle::sprintf("%s : %s", path, erc.message()));
  for (; it != bfs::recursive_directory_iterator(); ++it)
    action(it->path().string(), args...);
}

static
void
list_action(std::string const& path, bool verbose, bool fallback_xattrs)
{
  if (verbose)
    std::cout << "processing " << path << std::endl;
  bool dir = boost::filesystem::is_directory(path);
  char buf[4096];
  int sz = port_getxattr(
    path.c_str(), "user.infinit.auth", buf, 4095, fallback_xattrs);
  if (sz < 0)
    perror(path.c_str());
  else
  {
    buf[sz] = 0;
    std::stringstream ss;
    ss.str(buf);
    boost::optional<bool> dir_inherit;
    if (dir)
    {
      int sz = port_getxattr(
        path.c_str(), "user.infinit.auth.inherit", buf, 4095, fallback_xattrs);
      if (sz < 0)
        perror(path.c_str());
      else
      {
        buf[sz] = 0;
        dir_inherit = (buf == std::string("true"));
      }
    }
    try
    {
      // [{name: s, read: bool, write: bool}]
      std::stringstream output;
      output << path << ":" << std::endl;
      if (dir_inherit)
      {
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        output << "  inherit: "
               << (dir_inherit.get() ? "yes" : "no")
               << std::endl;
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif
      }
      elle::json::Json j = elle::json::read(ss);
      auto a = boost::any_cast<elle::json::Array>(j);
      for (auto& li: a)
      {
        auto d = boost::any_cast<elle::json::Object>(li);
        auto n = boost::any_cast<std::string>(d.at("name"));
        auto r = boost::any_cast<bool>(d.at("read"));
        auto w = boost::any_cast<bool>(d.at("write"));
        const char* mode = w ? (r ? "rw" : "w") : (r ? "r" : "none");
        output << "\t" << n << ": " << mode << std::endl;
      }
      std::cout << output.str() << std::endl;
    }
    catch (std::exception const& e)
    {
      std::cout << path << " : " << buf << std::endl;
    }
  }
}

static
void
set_action(std::string const& path,
           std::vector<std::string> users,
           std::string const& mode,
           bool inherit,
           bool disinherit,
           bool verbose,
           bool fallback_xattrs)
{
  if (verbose)
    std::cout << "processing " << path << std::endl;
  using namespace boost::filesystem;
  bool dir = is_directory(path);
  if (inherit || disinherit)
  {
    if (dir)
    {
      try
      {
        std::string value = inherit ? "true" : "false";
        check(port_setxattr, path, "user.infinit.auth.inherit", value,
              fallback_xattrs);
      }
      catch (elle::Error const& error)
      {
        ELLE_ERR("setattr (inherit) on %s failed: %s", path,
                 elle::exception_string());
      }
    }
  }
  if (!mode.empty())
  {
    for (auto& username: users)
    {
      auto set_attribute =
        [path, mode, fallback_xattrs] (std::string const& value)
        {
          check(port_setxattr, path, ("user.infinit.auth." + mode), value,
                fallback_xattrs);
        };
      try
      {
        set_attribute(username);
      }
      // XXX: Invalid argument could be something else... Find a way to
      // distinguish the different errors.
      catch (InvalidArgument const&)
      {
        try
        {
          set_attribute(public_key_from_username(username));
        }
        catch (InvalidArgument const&)
        {
          ELLE_ERR("setattr (mode: %s) on %s failed: %s", mode, path,
                   elle::exception_string());
          throw;
        }
      }
    }
  }
}

COMMAND(list)
{
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  bool fallback = flag(args, "fallback-xattrs");
  for (auto const& path: paths)
  {
    list_action(path, verbose, fallback);
    if (recursive)
      recursive_action(list_action, path, verbose, fallback);
  }
}

COMMAND(set)
{
  auto paths = mandatory<std::vector<std::string>>(args, "path", "file/folder");
  if (paths.empty())
    throw CommandLineError("missing path argument");
  auto users_ = optional<std::vector<std::string>>(args, "user");
  auto users = users_ ? users_.get() : std::vector<std::string>();
  std::vector<std::string> allowed_modes = {"r", "w", "rw", "none", ""};
  auto mode_ = optional(args, "mode");
  auto mode = mode_ ? mode_.get() : "";
  auto it = std::find(allowed_modes.begin(), allowed_modes.end(), mode);
  if (it == allowed_modes.end())
  {
    throw CommandLineError(
      elle::sprintf("mode must be one of: %s", allowed_modes));
  }
  if (!mode.empty() && users.empty())
    throw CommandLineError("must specify user when setting mode");
  bool inherit = flag(args, "enable-inherit");
  bool disinherit = flag(args, "disable-inherit");
  if (inherit && disinherit)
  {
    throw CommandLineError(
      "inherit and disable-inherit are exclusive");
  }
  if (!inherit && !disinherit && mode.empty())
    throw CommandLineError("no operation specified");
  std::vector<std::string> modes_map = {"setr", "setw", "setrw", "clear", ""};
  mode = modes_map[it - allowed_modes.begin()];
  bool recursive = flag(args, "recursive");
  bool verbose = flag(args, "verbose");
  bool fallback = flag(args, "fallback-xattrs");
  // Don't do any operations before checking paths.
  for (auto const& path: paths)
  {
    if ((inherit || disinherit) &&
      !recursive && !boost::filesystem::is_directory(path))
    {
      throw CommandLineError(elle::sprintf(
        "%s is not a directory, cannot %s inherit",
        path, inherit ? "enable" : "disable"));
    }
  }
  for (auto const& path: paths)
  {
    set_action(path, users, mode, inherit, disinherit, verbose, fallback);
    if (recursive)
    {
      recursive_action(
        set_action, path, users, mode, inherit, disinherit, verbose, fallback);
    }
  }
}

typedef boost::optional<std::vector<std::string>> OptVecStr;

static
OptVecStr
collate_users(OptVecStr const& combined,
              OptVecStr const& users,
              OptVecStr const& admins,
              OptVecStr const& groups)
{
  if (!combined && !users && !admins && !groups)
    return boost::none;
  std::vector<std::string> res;
  if (combined)
  {
    for (auto c: combined.get())
      res.push_back(c);
  }
  if (users)
  {
    for (auto u: users.get())
      res.push_back(u);
  }
  if (admins)
  {
    for (auto a: admins.get())
    {
      if (a[0] == admin_prefix)
        res.push_back(a);
      else
        res.push_back(elle::sprintf("%s%s", admin_prefix, a));
    }
  }
  if (groups)
  {
    for (auto g: groups.get())
    {
      if (g[0] == group_prefix)
        res.push_back(g);
      else
        res.push_back(elle::sprintf("%s%s", group_prefix, g));
    }
  }
  return res;
}

static
void
group_add_remove(std::string const& path,
                 std::string const& group,
                 std::string const& object,
                 std::string const& action,
                 bool fallback)
{
  if (!object.length())
    throw CommandLineError("empty user or group name");
  static const std::string base = "user.infinit.group.";
  std::string action_detail = is_admin(object) ? "admin" : "";
  std::string attr = elle::sprintf("%s%s%s", base, action, action_detail);
  auto set_attr = [&] (std::string const& identifier)
    {
      check(port_setxattr, path, attr, group + ":" + identifier, fallback);
    };
  std::string name = is_admin(object) ? object.substr(1) : object;
  try
  {
    set_attr(name);
  }
  catch (elle::Error const& e)
  {
    if (is_group(object))
    {
      throw elle::Error(elle::sprintf(
        "ensure group \"%s\" exists and path is in a volume", name.substr(1)));
    }
    else
    {
      try
      {
        set_attr(public_key_from_username(name));
      }
      catch (elle::Error const& e)
      {
        throw elle::Error(elle::sprintf(
          "ensure user \"%s\" exists and path is in a volume", name));
      }
    }
  }
}

COMMAND(group)
{
  bool create = flag(args, "create");
  bool delete_ = flag(args, "delete");
  bool list = flag(args, "show");
  auto group = mandatory<std::string>(args, "name", "group name");
  auto add_user = optional<std::vector<std::string>>(args, "add-user");
  auto add_admin = optional<std::vector<std::string>>(args, "add-admin");
  auto add_group = optional<std::vector<std::string>>(args, "add-group");
  auto add = optional<std::vector<std::string>>(args, "add");
  add = collate_users(add, add_user, add_admin, add_group);
  auto rem_user = optional<std::vector<std::string>>(args, "remove-user");
  auto rem_admin = optional<std::vector<std::string>>(args, "remove-admin");
  auto rem_group = optional<std::vector<std::string>>(args, "remove-group");
  auto rem = optional<std::vector<std::string>>(args, "remove");
  rem = collate_users(rem, rem_user, rem_admin, rem_group);
  int action_count = (create ? 1 : 0) + (delete_ ? 1 : 0) + (list ? 1 : 0)
                   + (add ? 1 : 0) + (rem ? 1 : 0);
  if (action_count == 0)
    throw CommandLineError("no action specified");
  if (action_count > 1)
    throw CommandLineError("specify only one action at a time");
  bool fallback = flag(args, "fallback-xattrs");
  std::string path =
    mandatory<std::string>(args, "path", "path in volume");
  if (!boost::filesystem::exists(path))
    throw elle::Error(elle::sprintf("path does not exist: %s", path));
  // Need to perform group actions on a directory in the volume.
  if (!boost::filesystem::is_directory(path))
    path = boost::filesystem::path(path).parent_path().string();
  if (create)
    check(port_setxattr, path, "user.infinit.group.create", group, fallback);
  if (delete_)
    check(port_setxattr, path, "user.infinit.group.delete", group, fallback);
  if (add)
  {
    for (auto const& obj: add.get())
      group_add_remove(path, group, obj, "add", fallback);
  }
  if (rem)
  {
    for (auto const& obj: rem.get())
      group_add_remove(path, group, obj, "remove", fallback);
  }
  if (list)
  {
    char res[16384];
    int sz = port_getxattr(
      path, "user.infinit.group.list." + group, res, 16384, fallback);
    if (sz >= 0)
    {
      res[sz] = 0;
      std::cout << res << std::endl;
    }
    else
    {
      throw elle::Error(elle::sprintf("unable to list group: %s", group));
    }
  }
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  program = argv[0];
  Mode::OptionDescription fallback_option = {
    "fallback-xattrs", bool_switch(), "fallback to alternate xattr mode "
    "if system xattrs are not suppported"
  };
  Mode::OptionDescription verbose_option = {
    "verbose", bool_switch(), "verbose output" };
  Modes modes {
    {
      "list",
      "List current ACL",
      &list,
      "--path PATHS",
      {
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "recursive,R", bool_switch(), "list recursively" },
        fallback_option,
        verbose_option,
      },
    },
    {
      "set",
      "Set ACL",
      &set,
      "--path PATHS [--user USERS]",
      {
        { "path,p", value<std::vector<std::string>>(), "paths" },
        { "user,u", value<std::vector<std::string>>(), elle::sprintf(
          "users and groups (prefix: %s<group>)", group_prefix) },
        { "mode,m", value<std::string>(), "access mode: r,w,rw,none" },
        { "enable-inherit,i", bool_switch(),
          "new files/folders inherit from their parent directory" },
        { "disable-inherit", bool_switch(),
          "new files/folders do not inherit from their parent directory" },
        { "recursive,R", bool_switch(), "apply recursively" },
        fallback_option,
        verbose_option,
      },
    },
    {
      "group",
      "Group control",
      &group,
      "--name NAME --path PATH",
      {
        { "name,n", value<std::string>(), "group name" },
        { "create,c", bool_switch(), "create the group" },
        { "show", bool_switch(), "list group users and administrators" },
        { "delete,d", bool_switch(), "delete an existing group" },
        { "add-user", value<std::vector<std::string>>(),
          "add users to group" },
        { "add-admin", value<std::vector<std::string>>(),
          "add administrators to group" },
        { "add-group", value<std::vector<std::string>>(),
          "add groups to group" },
        { "add", value<std::vector<std::string>>(), elle::sprintf(
          "add users, administrators and groups to group "
          "(prefix: %s<group>, %s<admin>)", group_prefix, admin_prefix) },
        { "remove-user", value<std::vector<std::string>>(),
          "remove users from group" },
        { "remove-admin", value<std::vector<std::string>>(),
          "remove administrators from group" },
        { "remove-group", value<std::vector<std::string>>(),
          "remove groups from group" },
        { "remove", value<std::vector<std::string>>(), elle::sprintf(
          "remove users, administrators and groups from group "
          "(prefix: %s<group>, %s<admin>)", group_prefix, admin_prefix) },
        { "path,p", value<std::string>(), "a path within the volume" },
        fallback_option,
        verbose_option,
      },
    }
  };
  return infinit::main("Infinit access control list utility", modes, argc, argv,
                       std::string("path"));

}
