#ifndef OCTREE_NODE_H
#define OCTREE_NODE_H

#include <array>
#include <memory>
#include <bitset>

namespace CGAL {
  namespace Octree {
    namespace Node {

      template<typename Value>
      class Node {

      public:

        typedef std::array<Node<Value>, 8> Child_list;
        typedef std::array<uint32_t, 3> Int_location;
        typedef std::bitset<3> Index;

      private:

        Value m_value;

        const Node<Value> *m_parent;

        uint8_t m_depth;
        Int_location m_location;

        std::unique_ptr<Child_list> m_children;

      public:

        Node(Node<Value> *parent = nullptr, Index index = 0) : m_parent(parent), m_depth(0), m_location({0, 0, 0}) {

          if (parent) {

            m_depth = parent->m_depth + 1;

            for (int i = 0; i < 3; i++)
              m_location[i] = (2 * m_location[i]) + index[i];

          }
        }

        void split() {

          m_children = std::make_unique<Child_list>();
          for (int index = 0; index < 8; index++) {

            (*m_children)[index] = std::move(Node<Value>(this, {index}));
          }
        }

        void unsplit() {

          m_children.reset();
        }

        Node<Value> &operator[](int index) {
          return (*m_children)[index];
        }

        const Node<Value> &operator[](int index) const {
          return (*m_children)[index];
        }

        const uint8_t &depth() const { return m_depth; }

        bool is_leaf() const { return (!m_children); }

        Int_location location() const {

          return m_location;
        }

        Index index() const {

          Index result{};

          result[0] = location()[0] & 1;
          result[1] = location()[1] & 1;
          result[2] = location()[2] & 1;

          return result;
        }

      };
    }

  }
}

#endif //OCTREE_NODE_H
