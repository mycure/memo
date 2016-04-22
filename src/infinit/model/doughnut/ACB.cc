#include <infinit/model/doughnut/ACB.hh>

#include <boost/iterator/zip_iterator.hpp>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>
#include <elle/utility/Move.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/SecretKey.hh>
#include <cryptography/hash.hh>

#include <reactor/exception.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/serialization.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.ACB");

DAS_MODEL_FIELDS(infinit::model::doughnut::ACLEntry,
                 (key, read, write, token));

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      DAS_MODEL_DEFINE(ACLEntry, (key, read, write, token),
                       DasACLEntry);
      DAS_MODEL_DEFINE(ACLEntry, (key, read, write),
                       DasACLEntryPermissions);
    }
  }
}

DAS_MODEL_DEFAULT(infinit::model::doughnut::ACLEntry,
                  infinit::model::doughnut::DasACLEntry);
// FAILS in binary mode
// DAS_MODEL_SERIALIZE(infinit::model::doughnut::ACB::ACLEntry);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*---------.
      | ACLEntry |
      `---------*/

      ACLEntry::ACLEntry(infinit::cryptography::rsa::PublicKey key_,
                              bool read_,
                              bool write_,
                              elle::Buffer token_)
        : key(std::move(key_))
        , read(read_)
        , write(write_)
        , token(std::move(token_))
      {}

      ACLEntry::ACLEntry(ACLEntry const& other)
        : key{other.key}
        , read{other.read}
        , write{other.write}
        , token{other.token}
      {}

      ACLEntry::ACLEntry(elle::serialization::SerializerIn& s)
        : ACLEntry(deserialize(s))
      {}

      ACLEntry
      ACLEntry::deserialize(elle::serialization::SerializerIn& s)
      {

        auto key = s.deserialize<cryptography::rsa::PublicKey>("key");
        auto read = s.deserialize<bool>("read");
        auto write = s.deserialize<bool>("write");
        auto token = s.deserialize<elle::Buffer>("token");
        return ACLEntry(std::move(key), read, write, std::move(token));

        /*
        DasACLEntry::Update content(s);
        return ACLEntry(std::move(content.key.get()),
                        content.read.get(),
                        content.write.get(),
                        std::move(content.token.get()));*/

      }

      void
      ACLEntry::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("key", key);
        s.serialize("read", read);
        s.serialize("write", write);
        s.serialize("token", token);
      }

      bool
      ACLEntry::operator == (ACLEntry const& b) const
      {
        return key == b.key && read == b.read && write && b.write
          && token == b.token;
      }

      /*-------------.
      | Construction |
      `-------------*/

      template <typename Block>
      BaseACB<Block>::BaseACB(Doughnut* owner,
                              elle::Buffer data,
                              boost::optional<elle::Buffer> salt)
        : BaseACB(owner, std::move(data), std::move(salt), owner->keys())
      {}

      template <typename Block>
      BaseACB<Block>::BaseACB(Doughnut* owner,
                              elle::Buffer data,
                              boost::optional<elle::Buffer> salt,
                              cryptography::rsa::KeyPair const& keys)
        : Super(owner, std::move(data), std::move(salt), keys)
        , _editor(-1)
        , _owner_token()
        , _acl_changed(true)
        , _data_version(-1)
        , _data_signature()
        , _world_readable(false)
        , _world_writable(false)
        , _deleted(false)
      {}

      template <typename Block>
      BaseACB<Block>::BaseACB(BaseACB<Block> const& other)
        : Super(other)
        , _editor(other._editor)
        , _owner_token(other._owner_token)
        , _acl_changed(other._acl_changed)
        , _acl_entries(other._acl_entries)
        , _acl_group_entries(other._acl_group_entries)
        , _group_version(other._group_version)
        , _data_version(other._data_version)
        , _data_signature(other._data_signature)
        , _world_readable(other._world_readable)
        , _world_writable(other._world_writable)
        , _deleted(other._deleted)
        , _sign_key(other._sign_key)
      {}

      /*--------.
      | Content |
      `--------*/

      template <typename Block>
      int
      BaseACB<Block>::version() const
      {
        return this->_data_version;
      }

      static void background_open(elle::Buffer & target,
                                  elle::Buffer const& src,
                                  infinit::cryptography::rsa::PrivateKey const& k)
      {
        static bool bg = elle::os::getenv("INFINIT_NO_BACKGROUND_DECODE", "").empty();
        if (bg)
        {
          reactor::Thread::NonInterruptible ni;
          reactor::background([&] {
              target = k.open(src);
          });
        }
        else
          target = k.open(src);
      }

      template <typename Block>
      elle::Buffer
      BaseACB<Block>::_decrypt_data(elle::Buffer const& data) const
      {
        if (this->world_readable())
          return this->_data;
        elle::Buffer secret_buffer;
        if (this->owner_private_key())
        {
          ELLE_DEBUG("%s: we are owner", *this);
          background_open(secret_buffer, this->_owner_token, *this->owner_private_key());
        }
        else if (!this->_acl_entries.empty())
        {
          // FIXME: factor searching the token
          for (auto const& e: this->_acl_entries)
          {
            if (e.key == this->doughnut()->keys().K())
              background_open(secret_buffer, e.token, this->doughnut()->keys().k());
          }
        }
        if (secret_buffer.empty())
        {
          int idx = 0;
          for (auto const& e: this->_acl_group_entries)
          {
            try
            {
              Group g(*this->doughnut(), e.key);
              auto keys = g.group_keys();
              int v = this->_group_version[idx];
              if (v >= signed(keys.size()))
              {
                ELLE_DEBUG("announced version %s bigger than size %s",
                           v, keys.size());
                ++idx;
                continue;
              }
              background_open(secret_buffer, e.token, keys[v].k());
            }
            catch (elle::Error const& e)
            {
              ELLE_DEBUG("error accessing group: %s", e);
            }
            ++idx;
          }
        }
        if (secret_buffer.empty())
        {
          // FIXME: better exceptions
          throw ValidationFailed("no read permissions");
        }
        static elle::Bench bench("bench.acb.decrypt_2", 10000_sec);
        elle::Bench::BenchScope bs(bench);
        auto secret = elle::serialization::json::deserialize
          <cryptography::SecretKey>(secret_buffer);
        ELLE_DUMP("%s: secret: %s", *this, secret);
        return secret.decipher(this->_data);
      }

      /*------------.
      | Permissions |
      `------------*/

      template <typename Block>
      void
      BaseACB<Block>::_set_world_permissions(bool read, bool write)
      {
        if (this->_world_readable == read && this->_world_writable == write)
          return;
        this->_world_readable = read;
        this->_world_writable = write;
        this->_acl_changed = true;
        this->_data_changed = true;
      }

      template <typename Block>
      std::pair<bool, bool>
      BaseACB<Block>::_get_world_permissions()
      {
        return std::make_pair(this->_world_readable, this->_world_writable);
      }

      template <typename Block>
      void
      BaseACB<Block>::set_group_permissions(
        cryptography::rsa::PublicKey const& key,
        bool read,
        bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permisions for %s: %s, %s",
                         *this, key, read, write);
        auto& acl_entries = this->_acl_group_entries;
        ELLE_DUMP("%s: ACL entries: %s", *this, acl_entries);
        auto it = std::find_if(
          acl_entries.begin(), acl_entries.end(),
          [&] (ACLEntry const& e) { return e.key == key; });
        if (it == acl_entries.end())
        {
          if (!read && !write)
          {
            ELLE_DUMP("%s: new user with no read or write permissions, "
                      "do nothing", *this);
            return;
          }
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // If the owner token is empty, this block was never pushed and
          // sealing will generate a new secret and update the token.
          // FIXME: the block will always be sealed anyway, why encrypt a token
          // now ?
          elle::Buffer token;
          Group g(*this->doughnut(), key);
          if (this->_owner_token.size())
          {
            auto secret = this->owner_private_key()->open(this->_owner_token);
            token = g.current_public_key().seal(secret);
          }
          acl_entries.emplace_back(ACLEntry(key, read, write, token));
          this->_group_version.push_back(g.version()-1);
          this->_acl_changed = true;
        }
        else
        {
          if (!read && !write)
          {
            ELLE_DEBUG_SCOPE("%s: user (%s) no longer has read or write "
                             "permissions, remove ACL entry", *this, key);
            acl_entries.erase(it);
            this->_group_version.erase(this->_group_version.begin()
              + (it - acl_entries.begin()));
            this->_acl_changed = true;
            return;
          }
          ELLE_DEBUG_SCOPE("%s: edit ACL entry", *this);
          if (it->read != read)
          {
            it->read = read;
            this->_acl_changed = true;
          }
          if (it->write != write)
          {
            it->write = write;
            this->_acl_changed = true;
          }
        }
      }
      template <typename Block>
      void
      BaseACB<Block>::set_permissions(cryptography::rsa::PublicKey const& key,
                           bool read,
                           bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permisions for %s: %s, %s",
                         *this, key, read, write);
        if (key == *this->owner_key())
          throw elle::Error("Cannot set permissions for owner");
        auto& acl_entries = this->_acl_entries;
        ELLE_DUMP("%s: ACL entries: %s", *this, acl_entries);
        auto it = std::find_if
          (acl_entries.begin(), acl_entries.end(),
           [&] (ACLEntry const& e) { return e.key == key; });
        if (it == acl_entries.end())
        {
          if (!read && !write)
          {
            ELLE_DUMP("%s: new user with no read or write permissions, "
                      "do nothing", *this);
            return;
          }
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // If the owner token is empty, this block was never pushed and
          // sealing will generate a new secret and update the token.
          // FIXME: the block will always be sealed anyway, why encrypt a token
          // now ?
          elle::Buffer token;
          if (this->_owner_token.size())
          {
            auto okey = this->owner_private_key();
            if (!okey)
              throw elle::Error("Owner key unavailable");
            auto secret = okey->open(this->_owner_token);
            token = key.seal(secret);
          }
          acl_entries.emplace_back(ACLEntry(key, read, write, token));
          this->_acl_changed = true;
        }
        else
        {
          if (!read && !write)
          {
            ELLE_DEBUG_SCOPE("%s: user (%s) no longer has read or write "
                             "permissions, remove ACL entry", *this, key);
            acl_entries.erase(it);
            this->_acl_changed = true;
            return;
          }
          ELLE_DEBUG_SCOPE("%s: edit ACL entry", *this);
          if (it->read != read)
          {
            it->read = read;
            this->_acl_changed = true;
          }
          if (it->write != write)
          {
            it->write = write;
            this->_acl_changed = true;
          }
        }
      }

      template <typename Block>
      void
      BaseACB<Block>::_set_permissions(model::User const& user_, bool read, bool write)
      {
        try
        {
          auto& user = dynamic_cast<User const&>(user_);
          if (user.name()[0] == '@')
            this->set_group_permissions(user.key(), read, write);
          else
            this->set_permissions(user.key(), read, write);
        }
        catch (std::bad_cast const&)
        {
          ELLE_ABORT("doughnut was passed a non-doughnut user.");
        }
      }

      template <typename Block>
      void
      BaseACB<Block>::_copy_permissions(blocks::ACLBlock& to)
      {
        Self* other = dynamic_cast<Self*>(&to);
        if (!other)
          throw elle::Error("Other block is not an ACB");
        // FIXME: better implementation
        for (auto const& e: this->_acl_entries)
        {
          if (e.key != *other->owner_key())
            other->set_permissions(e.key, e.read, e.write);
        }
        for (auto const& e: this->_acl_group_entries)
        {
          other->set_group_permissions(e.key, e.read, e.write);
        }
        if (*other->owner_key() != *this->owner_key())
          other->set_permissions(*this->owner_key(), true, true);
        other->_world_readable = this->_world_readable;
        other->_world_writable = this->_world_writable;
      }

      template <typename Block>
      std::vector<blocks::ACLBlock::Entry>
      BaseACB<Block>::_list_permissions(
        boost::optional<Model const&> model) const
      {
        auto make_user =
          [&] (cryptography::rsa::PublicKey const& k)
          -> std::unique_ptr<infinit::model::User>
          {
            try
            {
              auto version = elle_serialization_version(this->doughnut()->version());
              if (model)
                return model->make_user(
                  elle::serialization::json::serialize(k, version));
              else
                return elle::make_unique<doughnut::User>(k, "");
            }
            catch(elle::Error const& e)
            {
              ELLE_WARN("exception making user: %s", e);
              return nullptr;
            }
          };
        std::vector<ACB::Entry> res;
        auto owner = make_user(*this->owner_key());
        if (owner)
          res.emplace_back(std::move(owner), true, true);
        for (auto const& ent: this->_acl_entries)
        {
          auto user = make_user(ent.key);
          if (user)
            res.emplace_back(std::move(user), ent.read, ent.write);
        }
        for (auto const& ent: this->_acl_group_entries)
        {
          try
          {
            std::unique_ptr<model::User> user;
            if (!model)
              user.reset(new doughnut::User(ent.key, ""));
            else
              user = this->doughnut()->make_user(
                elle::serialization::json::serialize(ent.key));
            res.emplace_back(std::move(user), ent.read, ent.write);
          }
          catch(reactor::Terminate const& e)
          {
            throw;
          }
          catch(std::exception const& e)
          {
            ELLE_TRACE("Exception making user: %s", e);
            res.emplace_back(elle::make_unique<model::User>(), ent.read, ent.write);
          }
        }
        return res;
      }

      /*-----------.
      | Validation |
      `-----------*/

      template <typename Block>
      blocks::ValidationResult
      BaseACB<Block>::_validate(Model const& model) const
      {
        static elle::Bench bench("bench.acb._validate", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        ELLE_DEBUG("%s: validate owner part", *this)
          if (auto res = Super::_validate(model)); else
            return res;
        if (this->_world_writable)
          return blocks::ValidationResult::success();
        ELLE_ASSERT(this->data_signature() != elle::Buffer());
        ELLE_DEBUG_SCOPE("%s: validate author part", *this);
        ACLEntry* entry = nullptr;
        bool is_group_entry = false;
        int group_index = -1;
        if (this->_editor != -1)
        {
          ELLE_DEBUG_SCOPE("%s: check author has write permissions", *this);
          if (this->_editor < 0)
          {
            ELLE_DEBUG("%s: no ACL or no editor", *this);
            return blocks::ValidationResult::failure("no ACL or no editor");
          }
          if (this->_editor >= signed(this->_acl_entries.size()))
          {
            int gindex = this->_editor - this->_acl_entries.size();
            if (gindex >= signed(this->_acl_group_entries.size()))
            {
              ELLE_DEBUG("%s: editor index out of bounds", *this);
              return blocks::ValidationResult::failure
              ("editor index out of bounds");
            }
            entry = elle::unconst(&this->_acl_group_entries[gindex]);
            is_group_entry = true;
            group_index = gindex;
          }
          else
            entry = elle::unconst(&this->_acl_entries[this->_editor]);
          if (!entry->write)
          {
            ELLE_DEBUG("%s: no write permissions", *this);
            return blocks::ValidationResult::failure("no write permissions");
          }
        }
        ELLE_DEBUG("%s: check author signature, entry=%s, sig=%s",
                   *this, !!entry, this->data_signature())
        {
          if (is_group_entry)
          { // fetch latest key for group
            Group g(*this->doughnut(), entry->key);
            auto pubkeys = g.group_public_keys();
            if (group_index >= signed(this->_group_version.size()))
              return blocks::ValidationResult::failure("group_version array too short");
            auto key_index = this->_group_version[group_index];
            if (key_index >= signed(pubkeys.size()))
              return blocks::ValidationResult::failure("group key out of range");
            auto& key = pubkeys[key_index];
            ELLE_DEBUG("validating with group key %s: %s", key_index, key);
            if (!key.verify(this->data_signature(), *this->_data_sign()))
            {
              ELLE_DEBUG("%s: group author signature invalid", *this);
              return blocks::ValidationResult::failure("Incorrect group key signature");
            }
          }
          else
          {
            auto& key = entry ? entry->key : *this->owner_key();
            if (!key.verify(this->data_signature(), *this->_data_sign()))
            {
              ELLE_DEBUG("%s: author signature invalid", *this);
              return blocks::ValidationResult::failure
                ("author signature invalid");
            }
          }
        }
        return blocks::ValidationResult::success();
      }

      template <typename Block>
      blocks::ValidationResult
      BaseACB<Block>::_validate(Model const& model,
                                blocks::Block const& new_block) const
      {
        auto supval = Block::_validate(model, new_block);
        if (!supval)
          return supval;
        auto acb = dynamic_cast<Self const*>(&new_block);
        if (!acb)
          return blocks::ValidationResult::failure("New block is not an ACB");
        // check non-regression of group signature indexes
        if (acb->_group_version.size() != acb->_acl_group_entries.size())
          return blocks::ValidationResult::failure("Mismatch size in group entries");
        for (int i=0; i<signed(this->_group_version.size()); ++i)
        {
          for (int j=0; j<signed(acb->_group_version.size()); ++j)
          {
            if (this->_acl_group_entries[i].key == acb->_acl_group_entries[j].key)
            {
              if (this->_group_version[i] > acb->_group_version[j])
              {
                ELLE_TRACE("Group key index downgraded: %s -> %s",
                         this->_group_version, acb->_group_version);
                return blocks::ValidationResult::conflict(
                  "Group key index downgraded.");
              }
              break;
            }
          }
        }
        return blocks::ValidationResult::success();
      }

      template <typename T>
      static
      void
      null_deleter(T*)
      {}

      template <typename Block>
      void
      BaseACB<Block>::seal(boost::optional<int> version,
                           cryptography::SecretKey const& key)
      {
        this->_seal(version, key);
      }

      template <typename Block>
      void
      BaseACB<Block>::_seal(boost::optional<int> version)
      {
        this->_seal(version, {});
      }

      template <typename Block>
      void
      BaseACB<Block>::_seal(boost::optional<int> version,
                            boost::optional<cryptography::SecretKey const&> key)
      {
        static elle::Bench bench("bench.acb.seal", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        bool acl_changed = this->_acl_changed;
        bool data_changed = this->_data_changed;
        std::shared_ptr<infinit::cryptography::rsa::PrivateKey> sign_key;
        if (acl_changed)
        {
          static elle::Bench bench("bench.acb.seal.aclchange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ELLE_DEBUG_SCOPE("%s: ACL changed, seal", *this);
          this->_acl_changed = false;
          if (this->owner_private_key())
          {
            sign_key = this->owner_private_key();
            this->_editor = -1;
          }
          Super::_seal_okb();
          if (!data_changed)
            ++this->_data_version;
        }
        else
          ELLE_DEBUG("%s: ACL didn't change", *this);
        if (data_changed)
        {
          static elle::Bench bench("bench.acb.seal.datachange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ++this->_data_version;
          ELLE_TRACE_SCOPE("%s: data changed, seal version %s",
                           *this, this->_data_version);
          if (this->owner_private_key())
          {
            ELLE_DEBUG("we are owner");
            sign_key = this->owner_private_key();
            this->_editor = -1;
          }
          boost::optional<cryptography::SecretKey> secret;
          if (!key)
          {
            secret = cryptography::secretkey::generate(256);
            key = secret;
          }
          ELLE_DUMP("%s: new block secret: %s", *this, key.get());
          auto version = elle_serialization_version(this->doughnut()->version());
          auto secret_buffer =
            elle::serialization::json::serialize(key.get(), version);
          this->_owner_token = this->owner_key()->seal(secret_buffer);
          int idx = 0;
          for (auto& e: this->_acl_entries)
          {
            if (e.read)
              e.token = e.key.seal(secret_buffer);
            if (!sign_key && e.key == this->doughnut()->keys().K())
            {
              ELLE_DEBUG("we are editor %s", idx);
              this->_editor = idx;
              sign_key = this->doughnut()->keys().private_key();
            }
            ++idx;
          }
          for (auto& e: this->_acl_group_entries)
          {
            Group g(*this->doughnut(), e.key);
            if (e.read)
            {
              e.token = g.current_public_key().seal(secret_buffer);
              this->_group_version[idx - this->_acl_entries.size()] =
                g.version() - 1;
            }
            if (!sign_key)
            {
              try
              {
                auto kp = g.current_key();
                this->_editor = idx;
                ELLE_DEBUG("we are editor from group %s", g);
                sign_key = g.current_key().private_key();
              }
              catch (elle::Error const& e)
              {}
            }
            ++idx;
          }
          if (!sign_key && this->_world_writable)
          {
            ELLE_DEBUG("block is world writable");
            sign_key = this->doughnut()->keys().private_key();
          }
          if (!this->_world_readable)
            this->blocks::MutableBlock::data(
              key->encipher(this->data_plain()));
          else
            this->blocks::MutableBlock::data(this->data_plain());
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
        // Even if only the ACL was changed, we need to re-sign because the ACL
        // address is part of the signature.
        if (acl_changed || data_changed || version)
        {
          if (version)
            this->_data_version = *version;
          if (!sign_key)
          { // can happen if version is set but data is unchanged
            if (this->owner_private_key())
            {
              ELLE_DEBUG("we are owner");
              sign_key = this->owner_private_key();
              this->_editor = -1;
            }
          }
          int idx = 0;
          if (!sign_key)
          {
            for (auto& e: this->_acl_entries)
            {
              if (e.key == this->doughnut()->keys().K())
              {
                ELLE_DEBUG("we are editor %s", idx);
                this->_editor = idx;
                sign_key = this->doughnut()->keys().private_key();
              }
              ++idx;
            }
          }
          if (!sign_key)
          {
            for (auto& e: this->_acl_group_entries)
            {
              Group g(*this->doughnut(), e.key);
              try
              {
                auto kp = g.current_key();
                this->_editor = idx;
                ELLE_DEBUG("we are editor from group %s", g);
                sign_key = g.current_key().private_key();
              }
              catch (elle::Error const& e)
              {}
            }
            ++idx;
          }
          if (!sign_key && this->_world_writable)
          {
            ELLE_DEBUG("block is world writable");
            sign_key = this->doughnut()->keys().private_key();
          }
          if (!sign_key)
            throw ValidationFailed("not owner and no write permissions");
          ELLE_DEBUG_SCOPE("%s: sign data", *this);
          this->_data_signature = std::make_shared<typename Super::SignFuture>(
              sign_key->sign_async(
                *this->_data_sign(), this->doughnut()->version()));
          this->_sign_key = sign_key;
        }
        // restart signature process in case we were restored from storage
        if (!this->_data_signature->running()
          && this->_data_signature->value() == elle::Buffer())
        {
          ELLE_ASSERT(_sign_key);
          this->_data_signature = std::make_shared<typename Super::SignFuture>(
            _sign_key->sign_async(
              *this->_data_sign(), this->doughnut()->version()));
        }
        if (!this->_signature->running()
          && this->_signature->value() == elle::Buffer())
          this->_seal_okb({}, false);
      }

      template <typename Block>
      BaseACB<Block>::~BaseACB()
      {}

      template <typename Block>
      elle::Buffer const&
      BaseACB<Block>::data_signature() const
      {
        return this->_data_signature->value();
      }

      template <typename Block>
      BaseACB<Block>::OwnerSignature::OwnerSignature(BaseACB<Block> const& b)
        : Super::OwnerSignature(b)
        , _block(b)
      {}

      template <typename Block>
      void
      BaseACB<Block>::OwnerSignature::_serialize(
        elle::serialization::SerializerOut& s,
        elle::Version const& v)
      {
        s.serialize(
          // FIXME: no non-const overload for that version
          "acls", elle::unconst(this->_block.acl_entries()),
          elle::serialization::as<das::Serializer<DasACLEntryPermissions>>());
        if (v >= elle::Version(0, 4, 0))
        {
          s.serialize(
            // FIXME: no non-const overload for that version
            "group_acls", elle::unconst(this->_block.acl_group_entries()),
            elle::serialization::as<das::Serializer<DasACLEntryPermissions>>());
          s.serialize("world_readable", this->_block.world_readable());
          s.serialize("world_writable", this->_block.world_writable());
        }
      }

      template <typename Block>
      std::unique_ptr<typename BaseACB<Block>::Super::OwnerSignature>
      BaseACB<Block>::_sign() const
      {
        return elle::make_unique<OwnerSignature>(*this);
      }

      template<typename Block>
      model::blocks::RemoveSignature
      BaseACB<Block>::_sign_remove(Model& model) const
      {
        auto res = this->clone();
        auto acb = dynamic_cast<Self*>(res.get());
        ELLE_ASSERT(acb);
        acb->_deleted = true;
        acb->_data_changed = true;
        acb->seal();
        blocks::RemoveSignature rs;
        rs.block = std::move(res);
        return rs;
      }

      template<typename Block>
      blocks::ValidationResult
      BaseACB<Block>::_validate_remove(Model& model,
                                       blocks::RemoveSignature const& rs) const
      {
        ELLE_DUMP_SCOPE("%s: check %f validates removal", this, rs);
        if (!rs.block)
        {
          ELLE_DUMP("remove signature has no block");
          return blocks::ValidationResult::failure(
            "remove signature has no block");
        }
        auto mb = dynamic_cast<Self*>(rs.block.get());
        if (!mb)
        {
          ELLE_DUMP("remove signature block is not mutable");
          return blocks::ValidationResult::failure(
            "remove signature block is not mutable");
        }
        if (!mb->deleted())
        {
          ELLE_DUMP("remove signature block's not marked for deletion");
          return blocks::ValidationResult::failure(
            "remove signature block's not marked for deletion");
        }
        // FIXME: calling validate can change our address, and make
        // the validate(other) below fail
        auto valid = rs.block->validate(model);
        if (!valid)
        {
          ELLE_DUMP("remove signature block's is not valid");
          return valid;
        }
        if (this->version() >= mb->version())
        {
          ELLE_DUMP("removal version %s is not greater than local version %s",
                    mb->version(), this->version());
          return blocks::ValidationResult::conflict("invalid version");
        }
        if (!(valid = this->blocks::Block::validate(model, *mb)))
          return valid;
        return blocks::ValidationResult::success();
      }

      template <typename... T>
      auto zip(const T&... containers)
        -> boost::iterator_range<boost::zip_iterator<
             decltype(boost::make_tuple(std::begin(containers)...))>>
      {
        auto zip_begin = boost::make_zip_iterator(
          boost::make_tuple(std::begin(containers)...));
        auto zip_end = boost::make_zip_iterator(
          boost::make_tuple(std::end(containers)...));
        return boost::make_iterator_range(zip_begin, zip_end);
      }

      template <typename Block>
      std::unique_ptr<typename BaseACB<Block>::DataSignature>
      BaseACB<Block>::_data_sign() const
      {
        return elle::make_unique<DataSignature>(*this);
      }

      template <typename Block>
      BaseACB<Block>::DataSignature::DataSignature(BaseACB<Block> const& block)
        : _block(block)
      {}

      template <typename Block>
      void
      BaseACB<Block>::DataSignature::serialize(
        elle::serialization::Serializer& s_,
        elle::Version const& v)
      {
        // FIXME: Improve when split-serialization is added.
        ELLE_ASSERT(s_.out());
        auto& s = reinterpret_cast<elle::serialization::SerializerOut&>(s_);
        s.serialize("salt", this->_block.salt());
        s.serialize("key", *this->_block.owner_key());
        s.serialize("version", this->_block.data_version());
        s.serialize("data", this->_block.blocks::Block::data());
        s.serialize("owner_token", this->_block.owner_token());
        s.serialize("acl", this->_block.acl_entries());
        if (v >= elle::Version(0, 4, 0))
        {
          s.serialize("group_acl", this->_block.acl_group_entries());
          s.serialize("group_version", this->_block.group_version());
          s.serialize("deleted", this->_block.deleted());
        }
      }

      /*--------.
      | Content |
      `--------*/
      template <typename Block>
      void
      BaseACB<Block>::_stored()
      {
      }

      template <typename Block>
      bool
      BaseACB<Block>::operator ==(blocks::Block const& rhs) const
      {
        auto other_acb = dynamic_cast<Self const*>(&rhs);
        if (!other_acb)
          return false;
        if (this->_editor != other_acb->_editor)
          return false;
        if (this->_owner_token != other_acb->_owner_token)
          return false;
        if (this->_acl_entries != other_acb->_acl_entries)
          return false;
        if (this->_data_version != other_acb->_data_version)
          return false;
        //if (this->_data_signature->value() != other_acb->_data_signature->value())
        //  return false;
        if (this->_world_readable != other_acb->_world_readable)
          return false;
        if (this->_world_writable != other_acb->_world_writable)
          return false;
        if (this->_deleted != other_acb->_deleted)
          return false;
        return this->Super::operator ==(rhs);
      }

      /*--------------.
      | Serialization |
      `--------------*/

      template <typename Block>
      BaseACB<Block>::BaseACB(elle::serialization::SerializerIn& input,
                              elle::Version const& version)
        : Super(input, version)
        , _editor(-2)
        , _owner_token()
        , _acl_changed(false)
        , _data_version(-1)
        , _data_signature()
        , _world_readable(false)
        , _world_writable(false)
        , _deleted(false)
      {
        this->_serialize(input, version);
      }

      template <typename Block>
      void
      BaseACB<Block>::serialize(elle::serialization::Serializer& s,
                                elle::Version const& version)
      {
        Super::serialize(s, version);
        this->_serialize(s, version);
      }

      template <typename Block>
      void
      BaseACB<Block>::_serialize(elle::serialization::Serializer& s,
                                 elle::Version const& version)
      {
        if (!this->_data_signature)
          this->_data_signature = std::make_shared<typename Super::SignFuture>();
        s.serialize("editor", this->_editor);
        s.serialize("owner_token", this->_owner_token);
        s.serialize("acl", this->_acl_entries);
        s.serialize("data_version", this->_data_version);
        if (version < elle::Version(0, 4, 0))
          if (s.out())
          {
            auto value =
              elle::WeakBuffer(this->_data_signature->value()).range(4);
            s.serialize("data_signature", value);
          }
          else
          {
            elle::Buffer signature;
            s.serialize("data_signature", signature);
            auto versioned =
              elle::serialization::binary::serialize(elle::Version(0, 3, 0));
            versioned.size(versioned.size() + signature.size());
            memcpy(versioned.mutable_contents() + 4,
                   signature.contents(), signature.size());
            this->_data_signature = std::make_shared<typename Super::SignFuture>(
              std::move(versioned));
          }
        else
        {
          bool need_signature = !s.context().has<ACBDontWaitForSignature>();
          if (!need_signature)
          {
            if (s.out())
            {
              bool ready = !this->_data_signature->running();
              s.serialize("data_signature_ready", ready);
              if (ready)
                s.serialize("data_signature", this->_data_signature->value());
              else
              {
                bool is_doughnut_key =
                  *this->_sign_key == this->doughnut()->keys_shared()->k();
                s.serialize("data_signature_is_doughnut_key", is_doughnut_key);
                if (!is_doughnut_key)
                  s.serialize("data_signature_key", this->_sign_key);
              }
            }
            else
            {
              bool ready;
              s.serialize("data_signature_ready", ready);
              if (ready)
                s.serialize("data_signature", this->_data_signature->value());
              else
              {
                bool is_doughnut_key;
                s.serialize("data_signature_is_doughnut_key", is_doughnut_key);
                if (!is_doughnut_key)
                  s.serialize("data_signature_key", this->_sign_key);
                else
                  this->_sign_key = this->doughnut()->keys_shared()->private_key();
              }
            }
          }
          else
          {
            s.serialize("data_signature", this->_data_signature->value());
          }
        }
        if (version >= elle::Version(0, 4, 0))
        {
          s.serialize("world_readable", this->_world_readable);
          s.serialize("world_writable", this->_world_writable);
          s.serialize("group_acl", this->_acl_group_entries);
          s.serialize("group_version", this->_group_version);
          s.serialize("deleted", this->_deleted);
        }
      }

      template
      class BaseACB<blocks::ACLBlock>;

      template
      class BaseACB<blocks::GroupBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
      static const elle::TypeInfo::RegisterAbbrevation
      _acb_abbr("BaseACB<infinit::model::blocks::ACLBlock>", "ACB");
    }
  }
}
