#ifndef INFINIT_MODEL_DOUGHNUT_LOCAL_HXX
# define INFINIT_MODEL_DOUGHNUT_LOCAL_HXX

# include <reactor/for-each.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template <typename R, typename ... Args>
      R
      Local::broadcast(std::string const& name, Args&& ... args)
      {
        ELLE_LOG_COMPONENT("infinit.model.doughnut.Local");
        ELLE_TRACE_SCOPE("%s: broadcast %s", this, name);
        reactor::for_each_parallel(
          this->_peers,
          [&] (std::shared_ptr<Connection> c)
          {
            // Arguments taken by reference as they will be passed multiple
            // times.
            RPC<R (Args const& ...)> rpc(
              name,
              c->_channels,
              this->version(),
              c->_rpcs._key);
            // Workaround GCC 4.9 ICE: argument packs don't work through
            // lambdas.
            auto const f = std::bind(
              &RPC<R (Args const& ...)>::operator (),
              &rpc, std::ref(args)...);
            return RPCServer::umbrella(f);
          },
          elle::sprintf("%s: broadcast RPC %s", this, name));
      }
    }
  }
}

#endif
