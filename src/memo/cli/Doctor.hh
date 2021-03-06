#pragma once

#include <regex>

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Doctor
      : public Object<Doctor>
    {
    public:
      /// Default server to ping with.
      static std::string const connectivity_server;

      Doctor(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::all,
                                    cli::configuration,
                                    cli::connectivity,
                                    cli::log,
                                    cli::networking,
                                    cli::system));
      using Objects = decltype(elle::meta::list(cli::log));

      /*------------.
      | Mode: all.  |
      `------------*/
      Mode<Doctor,
           void (decltype(cli::ignore_non_linked = false),
                 decltype(cli::upnp_tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::upnp_udt_port = boost::optional<uint16_t>()),
                 decltype(cli::server = connectivity_server),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_all)>
      all;
      void
      mode_all(bool ignore_non_linked,
               boost::optional<uint16_t> upnp_tcp_port,
               boost::optional<uint16_t> upnp_udt_port,
               boost::optional<std::string> const& server,
               bool no_color,
               bool verbose);

      /*----------------------.
      | Mode: configuration.  |
      `----------------------*/
      Mode<Doctor,
           void (decltype(cli::ignore_non_linked = false),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_configuration)>
      configuration;
      void
      mode_configuration(bool ignore_non_linked,
                         bool no_color,
                         bool verbose);

      /*---------------------.
      | Mode: connectivity.  |
      `---------------------*/
      Mode<Doctor,
           void (decltype(cli::upnp_tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::upnp_udt_port = boost::optional<uint16_t>()),
                 decltype(cli::server = connectivity_server),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_connectivity)>
      connectivity;
      void
      mode_connectivity(boost::optional<uint16_t> upnp_tcp_port,
                        boost::optional<uint16_t> upnp_udt_port,
                        boost::optional<std::string> const& server,
                        bool no_color,
                        bool verbose);

      /*------------.
      | Mode: log.  |
      `------------*/
      class Log
        : public Object<Log, Doctor>
      {
      public:
        using Super = Object<Log, Doctor>;
        using Modes = decltype(elle::meta::list(
                                 cli::delete_,
                                 cli::list,
                                 cli::push));
        Log(Memo& memo);

        // log delete.
        Mode<Log,
             void (decltype(cli::all = false),
                   decltype(cli::match = elle::defaulted(std::regex{""})),
                   decltype(cli::number = 0)),
             decltype(modes::mode_delete)>
        delete_;
        void
        mode_delete(bool all,
                    elle::Defaulted<std::regex> const& match,
                    int number);

        // log list.
        Mode<Log,
             void (decltype(cli::match = elle::defaulted(std::regex{""}))),
             decltype(modes::mode_list)>
        list;
        void
        mode_list(elle::Defaulted<std::regex> const& match);

        // log push.
        Mode<Log,
             void (decltype(cli::network = boost::optional<std::string>{}),
                   decltype(cli::match = elle::defaulted(std::regex{""})),
                   decltype(cli::number = 2)),
             decltype(modes::mode_push)>
        push;
        /// Send the latest logs to infinit.
        ///
        /// @param network a family name
        /// @param match   a regex
        /// @param number  the number of files to send, 0 for unlimited.
        void
        mode_push(boost::optional<std::string> const& network,
                  elle::Defaulted<std::regex> const& match,
                  int number);

        std::string const description = "Manage logs";
      } log;


      /*-------------------.
      | Mode: networking.  |
      `-------------------*/
      Mode<Doctor,
           void (decltype(cli::mode = boost::optional<std::string>()),
                 decltype(cli::protocol = boost::optional<std::string>()),
                 decltype(cli::packet_size =
                          boost::optional<elle::Buffer::Size>()),
                 decltype(cli::packets_count = boost::optional<int64_t>()),
                 decltype(cli::host = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<uint16_t>()),
                 decltype(cli::tcp_port = boost::optional<uint16_t>()),
                 decltype(cli::utp_port = boost::optional<uint16_t>()),
                 decltype(cli::xored_utp_port = boost::optional<uint16_t>()),
                 decltype(cli::xored = boost::optional<std::string>("both")),
                 decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_networking)>
      networking;
      void
      mode_networking(boost::optional<std::string> const& mode_name,
                      boost::optional<std::string> const& protocol_name,
                      boost::optional<elle::Buffer::Size> packet_size,
                      boost::optional<int64_t> packets_count,
                      boost::optional<std::string> const& host,
                      boost::optional<uint16_t> port,
                      boost::optional<uint16_t> tcp_port,
                      boost::optional<uint16_t> utp_port,
                      boost::optional<uint16_t> xored_utp_port,
                      boost::optional<std::string> const& xored = std::string{"both"},
                      bool no_color = false,
                      bool verbose = false);


      /*---------------.
      | Mode: system.  |
      `---------------*/
      Mode<Doctor,
           void (decltype(cli::no_color = false),
                 decltype(cli::verbose = false)),
           decltype(modes::mode_system)>
      system;
      void
      mode_system(bool no_color,
                  bool verbose);
    };
  }
}
