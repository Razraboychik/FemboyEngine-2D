// FemboyEngine2D - flat map, standard when available.
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <version>
#include <vector>

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#define FBY_HAS_STD_FLAT_MAP 1
#else
#define FBY_HAS_STD_FLAT_MAP 0
#endif

namespace Fby
{

#if FBY_HAS_STD_FLAT_MAP

template <class Key, class Value, class Compare = std::less<Key>> using FlatMap = std::flat_map<Key, Value, Compare>;

#else

// Sorted-vector map. The interface is the subset of std::flat_map the engine
template <class Key, class Value, class Compare = std::less<Key>> class FlatMap
{
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using container_type = std::vector<value_type>;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using size_type = std::size_t;

    FlatMap() = default;
    explicit FlatMap(Compare InCompare) : MCompare(std::move(InCompare))
    {
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return MData.empty();
    }
    [[nodiscard]] size_type size() const noexcept
    {
        return MData.size();
    }
    void reserve(size_type InCapacity)
    {
        MData.reserve(InCapacity);
    }
    void clear() noexcept
    {
        MData.clear();
    }

    [[nodiscard]] iterator begin() noexcept
    {
        return MData.begin();
    }
    [[nodiscard]] iterator end() noexcept
    {
        return MData.end();
    }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return MData.begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return MData.end();
    }

    [[nodiscard]] iterator lower_bound(const Key& InKey)
    {
        return std::lower_bound(MData.begin(), MData.end(), InKey, KeyLess{MCompare});
    }
    [[nodiscard]] const_iterator lower_bound(const Key& InKey) const
    {
        return std::lower_bound(MData.begin(), MData.end(), InKey, KeyLess{MCompare});
    }
    [[nodiscard]] iterator upper_bound(const Key& InKey)
    {
        return std::upper_bound(MData.begin(), MData.end(), InKey, KeyLess{MCompare});
    }
    [[nodiscard]] const_iterator upper_bound(const Key& InKey) const
    {
        return std::upper_bound(MData.begin(), MData.end(), InKey, KeyLess{MCompare});
    }

    [[nodiscard]] iterator find(const Key& InKey)
    {
        iterator ScopedIt = lower_bound(InKey);
        return (ScopedIt != MData.end() && !MCompare(InKey, ScopedIt->first)) ? ScopedIt : MData.end();
    }
    [[nodiscard]] const_iterator find(const Key& InKey) const
    {
        const_iterator ScopedIt = lower_bound(InKey);
        return (ScopedIt != MData.end() && !MCompare(InKey, ScopedIt->first)) ? ScopedIt : MData.end();
    }
    [[nodiscard]] bool contains(const Key& InKey) const
    {
        return find(InKey) != MData.end();
    }
    [[nodiscard]] size_type count(const Key& InKey) const
    {
        return contains(InKey) ? 1u : 0u;
    }

    [[nodiscard]] Value& at(const Key& InKey)
    {
        iterator ScopedIt = find(InKey);
        if (ScopedIt == MData.end())
        {
            throw std::out_of_range("Fby::FlatMap::at");
        }
        return ScopedIt->second;
    }
    [[nodiscard]] const Value& at(const Key& InKey) const
    {
        const_iterator ScopedIt = find(InKey);
        if (ScopedIt == MData.end())
        {
            throw std::out_of_range("Fby::FlatMap::at");
        }
        return ScopedIt->second;
    }

    Value& operator[](const Key& InKey)
    {
        iterator ScopedIt = lower_bound(InKey);
        if (ScopedIt == MData.end() || MCompare(InKey, ScopedIt->first))
        {
            ScopedIt = MData.insert(ScopedIt, value_type{InKey, Value{}});
        }
        return ScopedIt->second;
    }

    std::pair<iterator, bool> insert(value_type InValue)
    {
        iterator ScopedIt = lower_bound(InValue.first);
        if (ScopedIt != MData.end() && !MCompare(InValue.first, ScopedIt->first))
        {
            return {ScopedIt, false};
        }
        return {MData.insert(ScopedIt, std::move(InValue)), true};
    }

    template <class... Args> std::pair<iterator, bool> try_emplace(const Key& InKey, Args&&... InArgs)
    {
        iterator ScopedIt = lower_bound(InKey);
        if (ScopedIt != MData.end() && !MCompare(InKey, ScopedIt->first))
        {
            return {ScopedIt, false};
        }
        ScopedIt = MData.insert(ScopedIt, value_type{InKey, Value(std::forward<Args>(InArgs)...)});
        return {ScopedIt, true};
    }

    std::pair<iterator, bool> insert_or_assign(const Key& InKey, Value InValue)
    {
        iterator ScopedIt = lower_bound(InKey);
        if (ScopedIt != MData.end() && !MCompare(InKey, ScopedIt->first))
        {
            ScopedIt->second = std::move(InValue);
            return {ScopedIt, false};
        }
        ScopedIt = MData.insert(ScopedIt, value_type{InKey, std::move(InValue)});
        return {ScopedIt, true};
    }

    size_type erase(const Key& InKey)
    {
        iterator ScopedIt = find(InKey);
        if (ScopedIt == MData.end())
        {
            return 0;
        }
        MData.erase(ScopedIt);
        return 1;
    }

    // Direct access to the sorted storage: systems that iterate every entry
    [[nodiscard]] const container_type& data() const noexcept
    {
        return MData;
    }

private:
    struct KeyLess
    {
        const Compare& compare;
        [[nodiscard]] bool operator()(const value_type& InEntry, const Key& InKey) const
        {
            return compare(InEntry.first, InKey);
        }
    };

    container_type MData;
    // A plain member: [[no_unique_address]] is ignored by the MSVC front end and
    Compare MCompare{};
};

// FBY_HAS_STD_FLAT_MAP
#endif
}
