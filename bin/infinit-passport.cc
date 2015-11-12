#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-passport");

#include <main.hh>

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

static
void
create(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  auto user_name = mandatory(args, "user", "user name");
  auto network = ifnt.network_get(network_name, self);
  auto user = ifnt.user_get(user_name);
  if (auto conf = dynamic_cast<
        infinit::model::doughnut::Configuration*>(network.model.get()))
  {
    if (self.public_key != conf->owner)
    {
      throw elle::Error(
        elle::sprintf("not owner of network \"%s\"", network_name));
    }
  }
  else
  {
    throw elle::Error(elle::sprintf(
      "unknown model configuration: %s", typeid(network.model.get()).name()));
  }
  infinit::model::doughnut::Passport passport(
    user.public_key,
    network.name,
    self.private_key.get());
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::serialize(passport, *output, false);
    report_action_output(
      *output, "wrote", "passport for", network.name);
  }
  else
  {
    ifnt.passport_save(passport);
    report_created("passport",
                   elle::sprintf("%s: %s", network.name, user_name));
  }
  if (aliased_flag(args, {"push-passport", "push"}))
  {
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", network.name, user.name),
      "passport",
      elle::sprintf("%s: %s", network.name, user.name),
      passport,
      self);
  }
}

static
void
export_(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto output = get_output(args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto passport = ifnt.passport_get(network_name, user_name);
  {
    elle::serialization::json::serialize(passport, *output, false);
  }
  report_exported(*output, "passport",
                  elle::sprintf("%s: %s", network_name, user_name));
}

static
void
fetch(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto network_name = optional(args, "network");
  if (network_name)
    network_name = ifnt.qualified_name(network_name.get(), self);
  auto user_name = optional(args, "user");
  if (network_name && user_name)
  {
    auto passport = beyond_fetch<infinit::Passport>(
      elle::sprintf("networks/%s/passports/%s",
                    network_name.get(), user_name.get()),
      "passport for",
      network_name.get());
    ifnt.passport_save(passport);
  }
  // Fetch all network passports if owner else fetch just the user's passport.
  else if (network_name)
  {
    auto owner_name =
      network_name.get().substr(0, network_name.get().find("/"));
    if (owner_name == self.name)
    {
      auto res = beyond_fetch_json(
        elle::sprintf("networks/%s/passports", network_name.get()),
        "passports for",
        network_name.get(),
        self);
      auto json = boost::any_cast<elle::json::Object>(res);
      for (auto const& user_passport: json)
      {
        elle::serialization::json::SerializerIn s(user_passport.second, false);
        auto passport = s.deserialize<infinit::Passport>();
        ifnt.passport_save(passport);
      }
    }
    else
    {
      auto passport = beyond_fetch<infinit::Passport>(elle::sprintf(
        "networks/%s/passports/%s", network_name.get(), self.name),
        "passport for",
        network_name.get(),
        self);
      ifnt.passport_save(passport);
    }
  }
  else if (user_name && user_name.get() != self.name)
  {
    throw CommandLineError("use the --as to fetch passports for another user");
  }
  // Fetch self passports.
  else
  {
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Passport>>>(
        elle::sprintf("users/%s/passports", self.name),
        "passports for user",
        self.name,
        self);
    for (auto const& passport: res["passports"])
    {
      try
      {
        ifnt.passport_save(passport);
      }
      catch (ResourceAlreadyFetched const&)
      {}
    }
  }
}

static
void
import(variables_map const& args)
{
  auto input = get_input(args);
  auto passport = elle::serialization::json::deserialize<infinit::Passport>
    (*input, false);
  ifnt.passport_save(passport);
  std::string user_name;
  for (auto const& user: ifnt.users_get())
  {
    if (user.public_key == passport.user())
    {
      user_name = user.name;
      break;
    }
  }
  report_imported("passport",
                  elle::sprintf("%s: %s", passport.network(), user_name));
}

static
void
push(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto passport = ifnt.passport_get(network_name, user_name);
  {
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", network_name, user_name),
      "passport",
      elle::sprintf("%s: %s", network_name, user_name),
      passport,
      self);
  }
}

static
void
pull(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  {
    beyond_delete(
      elle::sprintf("networks/%s/passports/%s", network_name, user_name),
      "passport for",
      user_name,
      self);
  }
}

static
void
list(variables_map const& args)
{
  namespace boost_fs = boost::filesystem;
  auto self = self_user(ifnt, args);
  auto network_name = optional(args, "network");
  boost_fs::path path;
  if (network_name)
  {
    network_name = ifnt.qualified_name(network_name.get(), self);
    path = ifnt._passport_path() / network_name.get();
  }
  else
  {
    path = ifnt._passport_path();
  }
  for (boost_fs::recursive_directory_iterator it(path);
       it != boost_fs::recursive_directory_iterator();
       ++it)
  {
    if (is_regular_file(it->status()))
    {
      auto user_name = it->path().filename().string();
      boost_fs::ifstream f;
      ifnt._open_read(f, it->path(), user_name, "passport");
      elle::serialization::json::SerializerIn s(f, false);
      auto passport =  s.deserialize<infinit::Passport>();
      std::cout << passport.network() << ": " << user_name << std::endl;
    }
  }
}

static
void
delete_(variables_map const& args)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto path = ifnt._passport_path(network_name, user_name);
  if (boost::filesystem::remove(path))
  {
    report_action("deleted", "passport",
                  elle::sprintf("%s: %s", network_name, user_name),
                  std::string("locally"));
  }
  else
  {
    throw elle::Error(
      elle::sprintf("File for passport could not be deleted: %s", path));
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  Modes modes {
    {
      "create",
      "Create a passport for a user to a network",
      &create,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(),
          "network to create the passport to." },
        { "user,u", value<std::string>(), "user to create the passport for" },
        { "push-passport", bool_switch(),
          elle::sprintf("push the passport to %s", beyond()).c_str() },
        { "push,p", bool_switch(), "alias for --push-passport" },
        option_output("passport"),
        option_owner,
      },
    },
    {
      "export",
      "Export a network",
      &export_,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network to export passport for" },
        { "user,u", value<std::string>(), "user to export passport for" },
        option_output("passport"),
        option_owner,
      },
    },
    {
      "fetch",
      "Fetch a passport for a user to a network",
      &fetch,
      "[--network NETWORK --user USER]",
      {
        { "network,n", value<std::string>(),
          "network to fetch the passport for" },
        { "user,u", value<std::string>(), "user to fetch passports for" },
        option_owner,
      },
    },
    {
      "import",
      "Import a passport for a user to a network",
      &import,
      "[--input INPUT]",
      {
        option_input("passport"),
      },
    },
    {
      "push",
      elle::sprintf("Push a user's passport to %s", beyond()).c_str(),
      &push,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a user's passport from %s", beyond()).c_str(),
      &pull,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
        option_owner,
      },
    },
    {
      "list",
      "List all local passports",
      &list,
      "[--network NETWORK]",
      {
        { "network,n", value<std::string>(),
          "network to list passports for (optional)" },
        option_owner,
      },
    },
    {
      "delete",
      "Locally delete a passport",
      &delete_,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
        option_owner,
      },
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}