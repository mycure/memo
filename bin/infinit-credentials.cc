#include <elle/log.hh>
#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

static const boost::regex _aws_access_key_regex("[A-Z0-9]{20}");
static const boost::regex _aws_secrect_key_regex("[A-Za-z0-9/+=]{40}");

static
std::string
read_key(std::string const& prompt_text, boost::regex const& regex)
{
  boost::smatch matches;
  std::string res;
  {
    bool first = true;
    while (!boost::regex_match(res, matches, regex))
    {
      if (!first)
      {
        std::cout << "Invalid \"" << prompt_text << "\", try again..."
                  << std::endl;
      }
      else
        first = false;
      std::cout << prompt_text << ": ";
      std::cout.flush();
      std::getline(std::cin, res);
    }
  }
  return res;
}

COMMAND(add)
{
  auto user = self_user(ifnt, args);
  if (args.count("dropbox"))
  {
    std::cout << "Register your Dropbox account with infinit by visiting "
              << beyond() << "/users/" << user.name
              << "/dropbox-oauth" << std::endl;
  }
  else if (args.count("google"))
  {
    std::cout << "Register your Google account with infinit by visiting "
              << beyond() << "/users/" << user.name
              << "/google-oauth" << std::endl;
  }
  else if (args.count("aws"))
  {
    auto account = mandatory(args, "account", "account name");
    std::cout << "Please enter your AWS credentials" << std::endl;
    std::string access_key_id =
      read_key("Access Key ID", _aws_access_key_regex);
    std::string secret_access_key =
      read_key("Secret Access Key", _aws_secrect_key_regex);
    auto aws_credentials = elle::make_unique<infinit::AWSCredentials>(
      account, access_key_id, secret_access_key);
    ifnt.credentials_aws_add(std::move(aws_credentials));
    report_action(
      "stored", "AWS credentials", account, std::string("locally"));
  }
  else
    throw CommandLineError("service type not specified");
}

struct Enabled
{
  bool aws;
  bool dropbox;
  bool google;
  bool multi;
};

Enabled
enabled(variables_map const& args)
{
  int aws = args.count("aws") ? 1 : 0;
  int dropbox = args.count("dropbox") ? 1 : 0;
  int google = args.count("google") ? 1 : 0;
  if (!aws && !dropbox && !google)
    aws = dropbox = google = 1;
  return Enabled {
    bool(aws), bool(dropbox), bool(google), aws + dropbox + google > 1 };
}

template <typename T>
void
fetch_credentials(infinit::User const& user,
                  std::string const& name,
                  std::string const& pretty,
                  std::function<void (std::unique_ptr<T>)> add)
{
  std::string where = elle::sprintf("users/%s/credentials/%s", user.name, name);
  // FIXME: Workaround for using std::unique_ptr.
  // Remove when serialization does not require copy.
  auto res = beyond_fetch_json(
    where, elle::sprintf("\"%s\" credentials for", name), user.name, user);
  auto root = boost::any_cast<elle::json::Object>(res);
  auto credentials_vec =
      boost::any_cast<std::vector<elle::json::Json>>(root["credentials"]);
  for (auto const& a_json: credentials_vec)
  {
    elle::serialization::json::SerializerIn input(a_json, false);
    auto a = elle::make_unique<T>(input.deserialize<T>());
    elle::printf("Fetched %s credentials %s (%s)\n",
                 pretty, a->uid(), a->display_name());
    add(std::move(a));
  }
}

COMMAND(fetch)
{
  auto e = enabled(args);
  if (((!e.dropbox && !e.google) || (e.dropbox ^ e.google)) && e.aws)
  {
    throw CommandLineError(elle::sprintf("AWS credentials are not stored on %s",
                           beyond(true)));
  }
  auto user = self_user(ifnt, args);
  if (e.dropbox)
    fetch_credentials<infinit::OAuthCredentials>(
      user, "dropbox", "Dropbox",
      [] (std::unique_ptr<infinit::OAuthCredentials> a)
      { ifnt.credentials_dropbox_add(std::move(a)); });
  if (e.google)
    fetch_credentials<infinit::OAuthCredentials>(
      user, "google", "Google Drive",
      [] (std::unique_ptr<infinit::OAuthCredentials> a)
      { ifnt.credentials_google_add(std::move(a)); });
  // FIXME: remove deleted ones
}

#define SYMBOL(Sym)                                                     \
namespace s                                                             \
{                                                                       \
  struct                                                                \
  {                                                                     \
    template <typename T>                                               \
    static                                                              \
    auto                                                                \
    get_attribute(T const& o)                                           \
      -> decltype(o.Sym)                                                \
    {                                                                   \
      return o.Sym;                                                     \
    }                                                                   \
                                                                        \
    template <typename T, typename ... Args>                            \
    static                                                              \
    auto                                                                \
    call_method(T const& o, Args&& ... args)                            \
      -> decltype(o.Sym(std::forward<Args>(args)...))                   \
    {                                                                   \
      return o.Sym(std::forward<Args>(args)...);                        \
    }                                                                   \
  } Sym;                                                                \
};                                                                      \

SYMBOL(aws);
SYMBOL(credentials_aws);
SYMBOL(dropbox);
SYMBOL(credentials_dropbox);
SYMBOL(google);
SYMBOL(credentials_google);

template <typename Service, typename Fetch>
void
list_(Enabled const& e,
      Service service,
      Fetch fetch,
      std::string const& service_name)
{
  if (!service.get_attribute(e))
    return;
  bool first = true;
  for (auto const& credentials: fetch.call_method(ifnt))
  {
    if (e.multi && first)
      std::cout << service_name << ":" << std::endl;
    if (e.multi)
      std::cout << "  ";
    std::cout << credentials->uid() << ": "
              << credentials->display_name() << std::endl;
    first = false;
  }
}

COMMAND(list)
{
  auto e = enabled(args);
  list_(e, s::aws, s::credentials_aws, "AWS");
  list_(e, s::dropbox, s::credentials_dropbox, "Dropbox");
  list_(e, s::google, s::credentials_google, "Google");
}

int
main(int argc, char** argv)
{
  options_description services_options("Services");
  services_options.add_options()
    ("dropbox", "Dropbox account credentials")
    ("google", "Google account credentials")
    ("aws", "Amazon Web Services account credentials")
    ;
  options_description aws_options("AWS account options");
  aws_options.add_options()
    ("account", value<std::string>(), "account name")
    ;
  program = argv[0];
  Modes modes {
    {
      "add",
      "Add credentials for a third-party service",
      &add,
      "SERVICE",
      {},
      {services_options, aws_options},
    },
    {
      "fetch",
      elle::sprintf("Fetch credentials from %s", beyond(true)),
      &fetch,
      "[SERVICE]",
      {},
      {services_options},
    },
    {
      "list",
      "List local credentials",
      &list,
      "[SERVICE]",
      {},
      {services_options},
    },
  };
  return infinit::main("Infinit third-party credentials utility",
                       modes, argc, argv);
}
