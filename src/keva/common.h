// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Copyright (c) 2018 Jianping Wu
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef H_BITCOIN_NAMES_COMMON
#define H_BITCOIN_NAMES_COMMON

#include <compat/endian.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>

#include <map>
#include <set>

class CKevaScript;
class CDBBatch;

typedef std::vector<unsigned char> valtype;

/** Whether or not name history is enabled.  */
extern bool fNameHistory;

/**
 * Construct a valtype (e. g., name) from a string.
 * @param str The string input.
 * @return The corresponding valtype.
 */
inline valtype
ValtypeFromString (const std::string& str)
{
  return valtype (str.begin (), str.end ());
}

/**
 * Convert a valtype to a string.
 * @param val The valtype value.
 * @return Corresponding string.
 */
inline std::string
ValtypeToString (const valtype& val)
{
  return std::string (val.begin (), val.end ());
}

/* ************************************************************************** */
/* CKevaData.  */

/**
 * Information stored for a name in the database.
 */
class CKevaData
{

private:

  /** The name's value.  */
  valtype value;

  /** The transaction's height.  Used for expiry.  */
  unsigned nHeight;

  /** The name's last update outpoint.  */
  COutPoint prevout;

  /**
   * The name's address (as script).  This is kept here also, because
   * that information is useful to extract on demand (e. g., in name_show).
   */
  CScript addr;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action)
  {
    READWRITE (value);
    READWRITE (nHeight);
    READWRITE (prevout);
    READWRITE (*(CScriptBase*)(&addr));
  }

  /* Compare for equality.  */
  friend inline bool
  operator== (const CKevaData& a, const CKevaData& b)
  {
    return a.value == b.value && a.nHeight == b.nHeight
            && a.prevout == b.prevout && a.addr == b.addr;
  }
  friend inline bool
  operator!= (const CKevaData& a, const CKevaData& b)
  {
    return !(a == b);
  }

  /**
   * Get the height.
   * @return The name's update height.
   */
  inline unsigned
  getHeight () const
  {
    return nHeight;
  }

  /**
   * Get the value.
   * @return The name's value.
   */
  inline const valtype&
  getValue () const
  {
    return value;
  }

  /**
   * Get the name's update outpoint.
   * @return The update outpoint.
   */
  inline const COutPoint&
  getUpdateOutpoint () const
  {
    return prevout;
  }

  /**
   * Get the address.
   * @return The name's address.
   */
  inline const CScript&
  getAddress () const
  {
    return addr;
  }

  /**
   * Check if the name is expired at the current chain height.
   * @return True iff the name is expired.
   */
  bool isExpired () const;

  /**
   * Check if the name is expired at the given height.
   * @param h The height at which to check.
   * @return True iff the name is expired at height h.
   */
  bool isExpired (unsigned h) const;

  /**
   * Set from a name update operation.
   * @param h The height (not available from script).
   * @param out The update outpoint.
   * @param script The name script.  Should be a name (first) update.
   */
  void fromScript (unsigned h, const COutPoint& out, const CKevaScript& script);

};

/* ************************************************************************** */
/* CNameHistory.  */

/**
 * Keep track of a name's history.  This is a stack of old CKevaData
 * objects that have been obsoleted.
 */
class CNameHistory
{

private:

  /** The actual data.  */
  std::vector<CKevaData> data;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action)
  {
    READWRITE (data);
  }

  /**
   * Check if the stack is empty.  This is used to decide when to fully
   * delete an entry in the database.
   * @return True iff the data stack is empty.
   */
  inline bool
  empty () const
  {
    return data.empty ();
  }

  /**
   * Access the data in a read-only way.
   * @return The data stack.
   */
  inline const std::vector<CKevaData>&
  getData () const
  {
    return data;
  }

  /**
   * Push a new entry onto the data stack.  The new entry's height should
   * be at least as high as the stack top entry's.  If not, fail.
   * @param entry The new entry to push onto the stack.
   */
  inline void
  push (const CKevaData& entry)
  {
    assert (data.empty () || data.back ().getHeight () <= entry.getHeight ());
    data.push_back (entry);
  }

  /**
   * Pop the top entry off the stack.  This is used when undoing name
   * changes.  The name's new value is passed as argument and should
   * match the removed entry.  If not, fail.
   * @param entry The name's value after undoing.
   */
  inline void
  pop (const CKevaData& entry)
  {
    assert (!data.empty () && data.back () == entry);
    data.pop_back ();
  }

};

/* ************************************************************************** */
/* CNameIterator.  */

/**
 * Interface for iterators over the name database.
 */
class CNameIterator
{

public:

  // Virtual destructor in case subclasses need them.
  virtual ~CNameIterator ();

  /**
   * Seek to a given lower bound.
   * @param start The name to seek to.
   */
  virtual void seek (const valtype& name) = 0;

  /**
   * Get the next name.  Returns false if no more names are available.
   * @param name Put the name here.
   * @param data Put the name's data here.
   * @return True if successful, false if no more names.
   */
  virtual bool next (valtype& name, CKevaData& data) = 0;

};

/* ************************************************************************** */
/* CNameCache.  */

/**
 * Cache / record of updates to the name database.  In addition to
 * new names (or updates to them), this also keeps track of deleted names
 * (when rolling back changes).
 */
class CKevaCache
{

private:

  /**
   * Special comparator class for names that compares by length first.
   * This is used to sort the cache entry map in the same way as the
   * database is sorted.
   */
  class NameComparator
  {
  public:
    inline bool operator() (const std::tuple<valtype, valtype> a,
                            const std::tuple<valtype, valtype> b) const
    {
      unsigned int aSize = std::get<0>(a).size() + std::get<1>(a).size();
      unsigned int bSize = std::get<0>(b).size() + std::get<1>(b).size();
      if (aSize != bSize) {
        return aSize < bSize;
      }
      return a < b;
    }
  };

public:

  /**
   * Type for expire-index entries.  We have to make sure that
   * it is serialised in such a way that ordering is done correctly
   * by height.  This is not true if we use a std::pair, since then
   * the height is serialised as byte-array with little-endian order,
   * which does not correspond to the ordering by actual value.
   */
#if 0
  class ExpireEntry
  {
  public:

    unsigned nHeight;
    valtype name;

    inline ExpireEntry ()
      : nHeight(0), name()
    {}

    inline ExpireEntry (unsigned h, const valtype& n)
      : nHeight(h), name(n)
    {}

    /* Default copy and assignment.  */

    template<typename Stream>
      inline void
      Serialize (Stream& s) const
    {
      /* Flip the byte order of nHeight to big endian.  */
      const uint32_t nHeightFlipped = htobe32 (nHeight);

      ::Serialize (s, nHeightFlipped);
      ::Serialize (s, name);
    }

    template<typename Stream>
      inline void
      Unserialize (Stream& s)
    {
      uint32_t nHeightFlipped;

      ::Unserialize (s, nHeightFlipped);
      ::Unserialize (s, name);

      /* Unflip the byte order.  */
      nHeight = be32toh (nHeightFlipped);
    }

    friend inline bool
    operator== (const ExpireEntry& a, const ExpireEntry& b)
    {
      return a.nHeight == b.nHeight && a.name == b.name;
    }

    friend inline bool
    operator!= (const ExpireEntry& a, const ExpireEntry& b)
    {
      return !(a == b);
    }

    friend inline bool
    operator< (const ExpireEntry& a, const ExpireEntry& b)
    {
      if (a.nHeight != b.nHeight)
        return a.nHeight < b.nHeight;

      return a.name < b.name;
    }

  };
#endif

  /**
   * Type of name entry map.  This is public because it is also used
   * by the unit tests.
   */
  typedef std::map<std::tuple<valtype, valtype>, CKevaData, NameComparator> EntryMap;

private:

  /** New or updated names.  */
  EntryMap entries;
  /** Deleted names.  */
  std::set<valtype> deleted;

#if 0
  /**
   * New or updated history stacks.  If they are empty, the corresponding
   * database entry is deleted instead.
   */
  std::map<valtype, CNameHistory> history;
#endif

#if 0
  /**
   * Changes to be performed to the expire index.  The entry is mapped
   * to either "true" (meaning to add it) or "false" (delete).
   */
  std::map<ExpireEntry, bool> expireIndex;
#endif

  friend class CCacheNameIterator;

public:

  inline void
  clear ()
  {
    entries.clear ();
    deleted.clear ();
#if 0
    history.clear ();
    expireIndex.clear ();
#endif
  }

  /**
   * Check if the cache is "clean" (no cached changes).  This also
   * performs internal checks and fails with an assertion if the
   * internal state is inconsistent.
   * @return True iff no changes are cached.
   */
  inline bool
  empty () const
  {
    if (entries.empty () && deleted.empty ()) {
#if 0
      assert (history.empty () && expireIndex.empty ());
#endif
      return true;
    }

    return false;
  }

  /* See if the given name is marked as deleted.  */
  inline bool
  isDeleted (const valtype& nameSpace, const valtype& key) const
  {
#if 0
    return (deleted.count(name) > 0);
#else
    return false;
#endif
  }

  /* Try to get a name's associated data.  This looks only
     in entries, and doesn't care about deleted data.  */
  bool get(const valtype& nameSpace, const valtype& key, CKevaData& data) const;

  bool GetNamespace(const valtype& nameSpace, CKevaData& data) const;

  /* Insert (or update) a name.  If it is marked as "deleted", this also
     removes the "deleted" mark.  */
  void set(const valtype& nameSpace, const valtype& key, const CKevaData& data);

  /* Delete a name.  If it is in the "entries" set also, remove it there.  */
  void remove(const valtype& nameSpace, const valtype& key);

  /* Return a name iterator that combines a "base" iterator with the changes
     made to it according to the cache.  The base iterator is taken
     ownership of.  */
  CNameIterator* iterateNames (CNameIterator* base) const;

#if 0
  /**
   * Query for an history entry.
   * @param name The name to look up.
   * @param res Put the resulting history entry here.
   * @return True iff the name was found in the cache.
   */
  bool getHistory (const valtype& name, CNameHistory& res) const;

  /**
   * Set a name history entry.
   * @param name The name to modify.
   * @param data The new history entry.
   */
  void setHistory (const valtype& name, const CNameHistory& data);
#endif

  /* Query the cached changes to the expire index.  In particular,
     for a given height and a given set of names that were indexed to
     this update height, apply possible changes to the set that
     are represented by the cached expire index changes.  */
  void updateNamesForHeight (unsigned nHeight, std::set<valtype>& names) const;

#if 0
  /* Add an expire-index entry.  */
  void addExpireIndex (const valtype& name, unsigned height);

  /* Remove an expire-index entry.  */
  void removeExpireIndex (const valtype& name, unsigned height);
#endif

  /* Apply all the changes in the passed-in record on top of this one.  */
  void apply (const CKevaCache& cache);

  /* Write all cached changes to a database batch update object.  */
  void writeBatch (CDBBatch& batch) const;

};

#endif // H_BITCOIN_NAMES_COMMON
