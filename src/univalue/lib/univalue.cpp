// Copyright 2014 BitPay Inc.
// Copyright 2015 Bitcoin Core Developers
// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <stdint.h>
#include <stdlib.h>
#include <type_traits>

#include "univalue.h"

const UniValue NullUniValue;

const std::string UniValue::boolTrueVal{"1"};

void UniValue::clear() noexcept
{
    typ = VNULL;
    val.clear();
    entries.clear();
    values.clear();
}

void UniValue::setNull() noexcept
{
    clear();
}

void UniValue::setBool(bool val_)
{
    clear();
    typ = VBOOL;
    if (val_)
        val = boolTrueVal;
}

static bool validNumStr(const std::string& s)
{
    std::string tokenVal;
    unsigned int consumed;
    enum jtokentype tt = getJsonToken(tokenVal, consumed, s.data(), s.data() + s.size());
    return (tt == JTOK_NUMBER);
}

void UniValue::setNumStr(const std::string& val_)
{
    if (!validNumStr(val_))
        return;

    clear();
    typ = VNUM;
    val = val_;
}
void UniValue::setNumStr(std::string&& val_) noexcept
{
    if (!validNumStr(val_))
        return;

    clear();
    typ = VNUM;
    val = std::move(val_);
}

template<typename Num>
void UniValue::setIntOrFloat(Num num)
{
    constexpr int bufSize = std::is_integral<Num>::value ? 32 : 64; // use 32 byte buffer for ints, 64 for double
    constexpr auto fmt =
            std::is_same<Num, double>::value
            ? "%1.16g"
            : (std::is_same<Num, int64_t>::value
               ? "%" PRId64
               : (std::is_same<Num, uint64_t>::value
                  ? "%" PRIu64
                    // this is here to enforce uint64_t, int64_t or double (if evaluated will fail at compile-time)
                  : throw std::runtime_error("Unexpected type")));
    if (std::is_floating_point<Num>::value) {
        // ensure not NaN or inf, which are not representable by the JSON Number type
        if (!std::isfinite(num))
            return;
    }
    std::array<char, bufSize> buf;
    int n = std::snprintf(buf.data(), size_t(bufSize), fmt, num); // C++11 snprintf always NUL terminates
    if (n <= 0 || n >= bufSize) // should never happen
        return;
    clear();
    typ = VNUM;
    val.assign(buf.data(), std::string::size_type(n));
}

void UniValue::setInt(uint64_t val_)
{
    setIntOrFloat(val_);
}

void UniValue::setInt(int64_t val_)
{
    setIntOrFloat(val_);
}

void UniValue::setFloat(double val_)
{
    setIntOrFloat(val_);
}

void UniValue::setStr(const std::string& val_)
{
    clear();
    typ = VSTR;
    val = val_;
}
void UniValue::setStr(std::string&& val_) noexcept
{
    clear();
    typ = VSTR;
    val = std::move(val_);
}

void UniValue::setArray() noexcept
{
    clear();
    typ = VARR;
}

void UniValue::setObject() noexcept
{
    clear();
    typ = VOBJ;
}

void UniValue::push_back(const UniValue& val_)
{
    if (typ != VARR)
        return;

    values.push_back(val_);
}

void UniValue::push_back(UniValue&& val_)
{
    if (typ != VARR)
        return;

    values.emplace_back(std::move(val_));
}

void UniValue::push_backV(const std::vector<UniValue>& vec)
{
    if (typ != VARR)
        return;

    values.insert(values.end(), vec.begin(), vec.end());
}
void UniValue::push_backV(std::vector<UniValue>&& vec)
{
    if (typ != VARR)
        return;

    values.reserve(std::max(values.size() + vec.size(), values.capacity()));
    for (auto & item : vec)
        values.emplace_back(std::move(item));
    vec.clear(); // clear vector now to be tidy with memory
}

void UniValue::__pushKV(const std::string& key, UniValue&& val_)
{
    entries.emplace_back(key, std::move(val_));
}

void UniValue::__pushKV(std::string&& key, UniValue&& val_)
{
    entries.emplace_back(std::move(key), std::move(val_));
}

void UniValue::__pushKV(std::string&& key, const UniValue& val_)
{
    entries.emplace_back(std::move(key), val_);
}

void UniValue::__pushKV(const std::string& key, const UniValue& val_)
{
    entries.emplace_back(key, val_);
}

void UniValue::pushKV(const std::string& key, const UniValue& val_, bool check)
{
    if (typ != VOBJ)
        return;
    if (check) {
        if (auto found = find(key)) {
            *found = val_;
            return;
        }
    }
    __pushKV(key, val_);
}
void UniValue::pushKV(const std::string& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return;
    if (check) {
        if (auto found = find(key)) {
            *found = std::move(val_);
            return;
        }
    }
    __pushKV(key, std::move(val_));
}
void UniValue::pushKV(std::string&& key, const UniValue& val_, bool check)
{
    if (typ != VOBJ)
        return;
    if (check) {
        if (auto found = find(key)) {
            *found = val_;
            return;
        }
    }
    __pushKV(std::move(key), val_);
}
void UniValue::pushKV(std::string&& key, UniValue&& val_, bool check)
{
    if (typ != VOBJ)
        return;
    if (check) {
        if (auto found = find(key)) {
            *found = std::move(val_);
            return;
        }
    }
    __pushKV(std::move(key), std::move(val_));
}

void UniValue::pushKVs(const UniValue& obj)
{
    if (typ != VOBJ || obj.typ != VOBJ)
        return;

    for (auto& entry : obj.entries)
        entries.emplace_back(entry);
}
void UniValue::pushKVs(UniValue&& obj)
{
    if (typ != VOBJ || obj.typ != VOBJ)
        return;

    for (auto& entry : obj.entries)
        entries.emplace_back(std::move(entry));
    obj.setObject(); // reset moved obj now to be tidy with memory.
}

const UniValue& UniValue::operator[](const std::string& key) const noexcept
{
    if (auto found = find(key)) {
        return *found;
    }
    return NullUniValue;
}

const UniValue& UniValue::operator[](size_t index) const noexcept
{
    switch (typ) {
    case VOBJ:
        if (index < entries.size())
            return entries[index].second;
        return NullUniValue;
    case VARR:
        if (index < values.size())
            return values[index];
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue& UniValue::front() const noexcept
{
    switch (typ) {
    case VOBJ:
        if (!entries.empty())
            return entries.front().second;
        return NullUniValue;
    case VARR:
        if (!values.empty())
            return values.front();
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue& UniValue::back() const noexcept
{
    switch (typ) {
    case VOBJ:
        if (!entries.empty())
            return entries.back().second;
        return NullUniValue;
    case VARR:
        if (!values.empty())
            return values.back();
        return NullUniValue;
    default:
        return NullUniValue;
    }
}

const UniValue* UniValue::find(const std::string& key) const noexcept {
    for (auto& entry : entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}
UniValue* UniValue::find(const std::string& key) noexcept {
    for (auto& entry : entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool UniValue::operator==(const UniValue& other) const noexcept
{
    // Type must be equal.
    if (typ != other.typ)
        return false;
    // Some types have additional requirements for equality.
    switch (typ) {
    case VBOOL:
    case VNUM:
    case VSTR:
        return val == other.val;
    case VARR:
        return values == other.values;
    case VOBJ:
        return entries == other.entries;
    case VNULL:
        break;
    }
    // Returning true is the default behavior, but this is not included as a default statement inside the switch statement,
    // so that the compiler warns if some type is not explicitly listed there.
    return true;
}

void UniValue::reserve(size_t n)
{
    switch (typ) {
    case VOBJ:
        entries.reserve(n);
        break;
    case VARR:
        values.reserve(n);
        break;
    default:
        break;
    }
}

const char *uvTypeName(UniValue::VType t) noexcept
{
    switch (t) {
    case UniValue::VNULL: return "null";
    case UniValue::VBOOL: return "bool";
    case UniValue::VOBJ: return "object";
    case UniValue::VARR: return "array";
    case UniValue::VSTR: return "string";
    case UniValue::VNUM: return "number";
    }

    // not reached
    return nullptr;
}
