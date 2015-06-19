#ifndef INFINIT_MODEL_BLOCKS_IMMUTABLE_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_IMMUTABLE_BLOCK_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class ImmutableBlock
        : public Block
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef ImmutableBlock Self;
        typedef Block Super;

      /*-------------.
      | Construction |
      `-------------*/
      protected:
        ImmutableBlock(Address address);
        ImmutableBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ImmutableBlock(elle::serialization::Serializer& input);
      };
    }
  }
}

#endif
