#include <infinit/model/doughnut/OKB.hh>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <cryptography/hash.hh>

#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.OKB");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      OKBHeader::OKBHeader(cryptography::rsa::KeyPair const& keys,
                           cryptography::rsa::KeyPair const& block_keys)
        : _key(block_keys.K())
        , _owner_key(keys.K())
        , _signature()
      {
        auto owner_key_buffer = elle::serialization::serialize
          <cryptography::rsa::PublicKey, elle::serialization::Json>
          (this->_owner_key);
        this->_signature = block_keys.k().sign(owner_key_buffer);
      }

      // FIXME
      static auto dummy_keys = cryptography::rsa::keypair::generate(2048);

      OKBHeader::OKBHeader()
        : _key(dummy_keys.K()) // FIXME
        , _owner_key(dummy_keys.K()) // FIXME
        , _signature()
      {}

      Address
      OKBHeader::_hash_address() const
      {
        auto key_buffer = elle::serialization::serialize
          <cryptography::rsa::PublicKey, elle::serialization::Json>(this->_key);
        auto hash =
          cryptography::hash(key_buffer, cryptography::Oneway::sha256);
        return Address(hash.contents());
      }

      bool
      OKBHeader::validate(Address const& address) const
      {
        auto expected_address = this->_hash_address();
        if (address != expected_address)
        {
          ELLE_DUMP("%s: address %x invalid, expecting %x",
                    *this, address, expected_address);
          return false;
        }
        else
          ELLE_DUMP("%s: address is valid", *this);
        auto owner_key_buffer = elle::serialization::serialize
          <cryptography::rsa::PublicKey, elle::serialization::Json>
          (this->_owner_key);
        if (!this->_key.verify(this->OKBHeader::_signature, owner_key_buffer))
          return false;
        else
          ELLE_DUMP("%s: owner key is valid", *this);
        return true;
      }

      void
      OKBHeader::serialize(elle::serialization::Serializer& input)
      {
        input.serialize("key", this->_owner_key);
        input.serialize("signature", this->_signature);
      }

      /*-------------.
      | Construction |
      `-------------*/

      template <typename Block>
      BaseOKB<Block>::BaseOKB(Doughnut* owner)
        : OKBHeader(owner->keys(), cryptography::rsa::keypair::generate(2048))
        , Super(this->_hash_address())
        , _version(-1)
        , _signature()
        , _doughnut(owner)
        , _data_plain()
        , _data_decrypted(true)
      {}

      /*--------.
      | Content |
      `--------*/

      template <typename Block>
      elle::Buffer const&
      BaseOKB<Block>::data() const
      {
        this->_decrypt_data();
        return this->_data_plain;
      }

      template <typename Block>
      void
      BaseOKB<Block>::data(elle::Buffer data)
      {
        this->_data_plain = std::move(data);
        this->_data_changed = true;
        this->_data_decrypted = true;
      }

      template <typename Block>
      void
      BaseOKB<Block>::data(std::function<void (elle::Buffer&)> transformation)
      {
        this->_decrypt_data();
        transformation(this->_data_plain);
        this->_data_changed = true;
      }

      template <typename Block>
      void
      BaseOKB<Block>::_decrypt_data() const
      {
        if (!this->_data_decrypted)
        {
          ELLE_TRACE_SCOPE("%s: decrypt data", *this);
          const_cast<BaseOKB<Block>*>(this)->_data_plain =
            this->_decrypt_data(this->_data);
          ELLE_DUMP("%s: decrypted data: %s", *this, this->_data_plain);
          const_cast<BaseOKB<Block>*>(this)->_data_decrypted = true;
        }
      }

      template <typename Block>
      elle::Buffer
      BaseOKB<Block>::_decrypt_data(elle::Buffer const& data) const
      {
        return this->doughnut()->keys().k().open(data);
      }

      /*-----------.
      | Validation |
      `-----------*/

      template <typename Block>
      elle::Buffer
      BaseOKB<Block>::_sign() const
      {
        elle::Buffer res;
        {
          // FIXME: use binary to sign
          elle::IOStream output(res.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("block_key", this->_key);
          s.serialize("version", this->_version);
          this->_sign(s);
        }
        return res;
      }

      template <typename Block>
      void
      BaseOKB<Block>::_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize("data", this->_data);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal()
      {
        if (this->_data_changed)
        {
          ELLE_DEBUG_SCOPE("%s: data changed, seal", *this);
          ELLE_DUMP("%s: data: %s", *this, this->_data_plain);
          auto encrypted =
            this->doughnut()->keys().K().seal(this->_data_plain);
          ELLE_DUMP("%s: encrypted data: %s", *this, encrypted);
          this->Block::data(std::move(encrypted));
          this->_seal_okb();
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_seal_okb()
      {
        ++this->_version; // FIXME: idempotence in case the write fails ?
        auto sign = this->_sign();
        this->_signature = this->_doughnut->keys().k().sign(sign);
        ELLE_DUMP("%s: sign %s with %s: %f",
                  *this, sign, this->_doughnut->keys().k(), this->_signature);
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_validate(blocks::Block const& previous) const
      {
        if (!this->_validate())
          return false;
        if (!this->_validate_version<BaseOKB<Block>>
            (previous, &BaseOKB<Block>::_version, this->version()))
          return false;
        return true;
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_validate() const
      {
        if (!static_cast<OKBHeader const*>(this)->validate(this->address()))
          return false;
        auto sign = this->_sign();
        if (!this->_check_signature
            (this->_owner_key, this->_signature, sign, "owner"))
          return false;
        return true;
      }

      template <typename Block>
      bool
      BaseOKB<Block>::_check_signature(cryptography::rsa::PublicKey const& key,
                            elle::Buffer const& signature,
                            elle::Buffer const& data,
                            std::string const& name) const
      {
        ELLE_DUMP("%s: check %f signs %s with %s",
                  *this, signature, data, key);
        if (!key.verify(signature, data))
        {
          ELLE_TRACE("%s: %s signature is invalid", *this, name);
          return false;
        }
        else
        {
          ELLE_DUMP("%s: %s signature is valid", *this, name);
          return true;
        }
      }

      /*--------------.
      | Serialization |
      `--------------*/

      template <typename Block>
      BaseOKB<Block>::BaseOKB(elle::serialization::Serializer& input)
        : OKBHeader()
        , Super(input)
        , _version(-1)
        , _signature()
        , _doughnut(nullptr)
        , _data_plain()
        , _data_decrypted(false)
      {
        this->_serialize(input);
      }

      template <typename Block>
      void
      BaseOKB<Block>::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      template <typename Block>
      void
      BaseOKB<Block>::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize_context<Doughnut*>(this->_doughnut);
        ELLE_ASSERT(this->_doughnut);
        s.serialize("key", this->_key);
        s.serialize("owner", static_cast<OKBHeader&>(*this));
        s.serialize("version", this->_version);
        s.serialize("signature", this->_signature);
      }

      template
      class BaseOKB<blocks::MutableBlock>;
      template
      class BaseOKB<blocks::ACLBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<OKB> _register_okb_serialization("OKB");
    }
  }
}
