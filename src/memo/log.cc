#include <memo/log.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/irange.hpp>

#include <elle/archive/archive.hh>
#include <elle/fstream.hh> // rotate_versions.
#include <elle/log.hh>

#include <memo/utility.hh>

ELLE_LOG_COMPONENT("memo.log");

namespace memo
{
  bfs::path log_dir()
  {
    return canonical_folder(xdg_cache_home() / "logs");
  }

  std::string log_base(std::string const& base)
  {
    auto const path = log_dir() / base;
    auto const dir = path.parent_path().empty() ? "." : path.parent_path();
    create_directories(dir);
    return path.string();
  }

  void make_log(std::string const& base)
  {
    auto const level =
      memo::getenv("LOG_LEVEL",
                   "*athena*:DEBUG,*cli*:DEBUG,*model*:DEBUG"
                   ",*grpc*:DEBUG,*prometheus:LOG"s);
    auto const spec =
      elle::print("file://{file}?"
                  "time,microsec,"
                  "append,size=64MiB,rotate=15,"
                  "{level}",
                  {
                    {"file", log_base(base)},
                    {"level", level},
                  });
    ELLE_DUMP("building log: {}", spec);
    auto logger = elle::log::make_logger(spec);
    auto const dashes = std::string(80, '-') + '\n';
    logger->message(elle::log::Logger::Level::log,
                    elle::log::Logger::Type::warning,
                    _trace_component_,
                    dashes + dashes + dashes
                    + "starting memo " + version_describe(),
                    __FILE__, __LINE__, "Memo::Memo");
    elle::log::logger_add(std::move(logger));
  }

  void make_main_log()
  {
    make_log("main");
  }

  std::vector<bfs::path>
  latest_logs(std::string const& base, int n)
  {
    // The greatest NUM in logs/main.<NUM> file names.
    auto const last = [&base]() -> boost::optional<int>
      {
        auto const nums = elle::rotate_versions(log_base(base));
        if (nums.empty())
          return {};
        else
          return *boost::max_element(nums);
      }();
    auto res = std::vector<bfs::path>{};
    if (last)
    {
      // Get the `n` latest logs in the log directory, if they are
      // consecutive (i.e., don't take main.1 with main.3).
      for (auto i: boost::irange(*last, *last - n, -1))
      {
        auto const name = elle::print("{}.{}", log_base(base), i);
        if (bfs::exists(name))
          res.emplace_back(name);
        else
          break;
      }
    }
    return res;
  }

  boost::container::flat_set<std::string>
  log_families()
  {
    auto res = boost::container::flat_set<std::string>{};
    for (auto const& p: bfs::recursive_directory_iterator(log_dir()))
      if (is_visible_file(p))
      {
        // Check that name = "~/.cache/infinit/memo/logs/foo/base.123",
        // keep "foo/base".
        auto const name = p.path().string().substr(log_dir().size() + 1);
        auto const dot = name.rfind('.');
        if (dot == name.npos
            || !boost::all(name.substr(dot + 1), boost::is_digit()))
          ELLE_DUMP("ignoring file: {}", name);
        else
          res.emplace(name.substr(0, dot));
      }
    return res;
  }

  void
  log_remove_all()
  {
    auto ps = std::vector<bfs::path>{};
    for (auto const& p: bfs::recursive_directory_iterator(log_dir()))
      if (is_visible_file(p))
        ps.emplace_back(std::move(p));
    for (auto const& p: ps)
      elle::try_remove(p);
  }

  bool tar_logs(bfs::path const& tgz, std::string const& base, int n)
  {
    auto const files = latest_logs(base, n);
    if (files.empty())
    {
      ELLE_LOG("there are no log files");
      return false;
    }
    else
    {
      ELLE_DUMP("generating {} containing {}", tgz, files);
      archive(elle::archive::Format::tar_gzip, files, tgz);
      return true;
    }
  }
}
