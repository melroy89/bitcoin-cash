// Copyright (c) 2019 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RADIX_H
#define BITCOIN_RADIX_H

#include <rcu.h>
#include <util.h>

#include <boost/noncopyable.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * This is a radix tree storing values identified by a unique key.
 *
 * The tree is composed of nodes (RadixNode) containing an array of
 * RadixElement. The key is split into chunks of a few bits that serve as an
 * index into that array. RadixElement is a discriminated union of either a
 * RadixNode* representing the next level in the tree, or a T* representing a
 * leaf. New RadixNode are added lazily when two leaves would go in the same
 * slot.
 *
 * Reads walk the tree using sequential atomic loads, and insertions are done
 * using CAS, which ensures both can be executed lock free. Removing any
 * elements from the tree can also be done using CAS, but requires waiting for
 * other readers before being destroyed. The tree uses RCU to track which thread
 * is reading the tree, which allows deletion to wait for other readers to be up
 * to speed before destroying anything. It is therefore crucial that the lock be
 * taken before reading anything in the tree.
 *
 * It is not possible to delete anything from the tree at this time. The tree
 * itself cannot be destroyed and will leak memory instead of cleaning up after
 * itself. This obviously needs to be fixed in subsequent revisions.
 */
template <typename T> struct RadixTree : public boost::noncopyable {
private:
    static const int BITS = 4;
    static const int MASK = (1 << BITS) - 1;
    static const size_t CHILD_PER_LEVEL = 1 << BITS;

    typedef decltype(std::declval<T &>().getId()) K;
    static const size_t KEY_BITS = 8 * sizeof(K);
    static const uint32_t TOP_LEVEL = (KEY_BITS - 1) / BITS;

    struct RadixElement;
    struct RadixNode;

    std::atomic<RadixElement> root;

public:
    RadixTree() : root(RadixElement()) {}

    /**
     * Insert a value into the tree.
     * Returns true if the value was inserted, false if it was already present.
     */
    bool insert(T *value) { return insert(value->getId(), value); }

    /**
     * Get the value corresponding to a key.
     * Returns the value if found, null if not.
     */
    T *get(const K &key) {
        uint32_t level = TOP_LEVEL;

        RCULock lock;
        RadixElement e = root.load();

        // Find a leaf.
        while (e.isNode()) {
            e = e.getNode()->get(level--, key)->load();
        }

        T *leaf = e.getLeaf();
        if (leaf == nullptr || leaf->getId() != key) {
            // We failed to find the proper element.
            return nullptr;
        }

        // The leaf is non-null and the keys match. We have our guy.
        return leaf;
    }

    const T *get(const K &key) const {
        return const_cast<RadixTree *>(this)->get(key);
    }

private:
    bool insert(const K &key, T *value) {
        uint32_t level = TOP_LEVEL;

        RCULock lock;
        std::atomic<RadixElement> *eptr = &root;

        while (true) {
            RadixElement e = eptr->load();

            // Walk down the tree until we find a leaf for our node.
            while (e.isNode()) {
            Node:
                eptr = e.getNode()->get(level--, key);
                e = eptr->load();
            }

            // If the slot is empty, try to insert right there.
            if (e.getLeaf() == nullptr) {
                if (eptr->compare_exchange_strong(e, RadixElement(value))) {
                    return true;
                }

                // CAS failed, we may have a node in there now.
                if (e.isNode()) {
                    goto Node;
                }
            }

            // The element was already in the tree.
            const K &leafKey = e.getLeaf()->getId();
            if (key == leafKey) {
                return false;
            }

            // There is an element there, but it isn't a subtree. We need to
            // convert it into a subtree and resume insertion into that subtree.
            std::unique_ptr<RadixNode> newChild =
                MakeUnique<RadixNode>(level, leafKey, e);
            if (eptr->compare_exchange_strong(e,
                                              RadixElement(newChild.get()))) {
                // We have a subtree, resume normal operations from there.
                newChild.release();
            }
        }
    }

    struct RadixElement {
    private:
        union {
            RadixNode *node;
            T *leaf;
            uintptr_t raw;
        };

        static const uintptr_t DISCRIMINANT = 0x01;
        bool getDiscriminent() const { return (raw & DISCRIMINANT) != 0; }

    public:
        explicit RadixElement() : raw(DISCRIMINANT) {}
        explicit RadixElement(RadixNode *nodeIn) : node(nodeIn) {}
        explicit RadixElement(T *leafIn) : leaf(leafIn) { raw |= DISCRIMINANT; }

        /**
         * Node features.
         */
        bool isNode() const { return !getDiscriminent(); }

        RadixNode *getNode() {
            assert(isNode());
            return node;
        }

        const RadixNode *getNode() const {
            assert(isNode());
            return node;
        }

        /**
         * Leaf features.
         */
        bool isLeaf() const { return getDiscriminent(); }

        T *getLeaf() {
            assert(isLeaf());
            return reinterpret_cast<T *>(raw & ~DISCRIMINANT);
        }

        const T *getLeaf() const {
            assert(isLeaf());
            return const_cast<RadixElement *>(this)->getLeaf();
        }
    };

    struct RadixNode {
    private:
        union {
            std::array<std::atomic<RadixElement>, CHILD_PER_LEVEL> childs;
            std::array<RadixElement, CHILD_PER_LEVEL>
                non_atomic_childs_DO_NOT_USE;
        };

    public:
        RadixNode(uint32_t level, const K &key, RadixElement e)
            : non_atomic_childs_DO_NOT_USE() {
            get(level, key)->store(e);
        }

        std::atomic<RadixElement> *get(uint32_t level, const K &key) {
            return &childs[(key >> (level * BITS)) & MASK];
        }
    };

    // Make sure the alignment works for T and RadixElement.
    static_assert(alignof(T) > 1, "T's alignment must be 2 or more.");
    static_assert(alignof(RadixNode) > 1,
                  "RadixNode alignment must be 2 or more.");
};

#endif // BITCOIN_RADIX_H