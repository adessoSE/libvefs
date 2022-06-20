
=========================================
 Hash Algorithm and Type Hashing Support
=========================================

::

    #include <vefs/hash/hash_algorithm.hpp>
    namespace vefs {}

.. namespace:: vefs


User facing APIs
----------------

.. function:: template <hash_algorithm Algorithm, typename T> \
              void hash_update(Algorithm &hashState, T const &object)

    This `tag_invoke <https://docs.deeplex.net/concrete/master/modules/tag_invoke.html>`_
    based customization point defines how instances of a type should be ingested
    by hash algorithms.

    It is already specialized for all :expr:`trivially_hashable` types.

    Example specialization::

        template <hash_algorithm Algorithm, typename Char, typename Traits>
        inline void tag_invoke(vefs::hash_update_fn,
                               Algorithm &hashState,
                               std::basic_string_view<Char, Traits> str) noexcept
        {
            if (!str.empty())
            {
                hashState.update(reinterpret_cast<std::byte const *>(str.data()),
                                 str.size());
            }
        }

.. function:: template <hash_algorithm Algorithm, unsigned_integer H, typename T> \
              auto hash(T const &object) -> H

    This `tag_invoke <https://docs.deeplex.net/concrete/master/modules/tag_invoke.html>`_
    based customization point defines how instances of a type should directly
    hashed which is usually more efficient than using the streaming API. However,
    there exists a fallback implementation for types which implement the 
    streaming API :expr:`hash_update`.
    
    It is already specialized for all :expr:`trivially_hashable` types.

    Example specialization::

        template <hash_algorithm Algorithm,
                  dplx::cncr::unsigned_integer H,
                  typename Char,
                  typename Traits>
        inline auto tag_invoke(hash_fn<Algorithm, H>,
                               std::basic_string_view<Char, Traits> const &str) noexcept
        {
            return Algorithm::template hash<H>(
                    reinterpret_cast<std::byte const *>(str.data()), str.size());
        }

.. function:: template <keyable_hash_algorithm Algorithm, unsigned_integer H, typename T> \
              auto hash(typename Algorithm::key_type const &key, T const &object) -> H

    This `tag_invoke <https://docs.deeplex.net/concrete/master/modules/tag_invoke.html>`_
    based customization point defines how instances of a type should directly
    hashed with a key which is usually more efficient than using the streaming 
    API. However, there exists a fallback implementation for types which
    implement the streaming API :expr:`hash_update`.
    
    It is already specialized for all :expr:`trivially_hashable` types.

    Example specialization::

        template <hash_algorithm Algorithm,
                  dplx::cncr::unsigned_integer H,
                  typename Char,
                  typename Traits>
        inline auto tag_invoke(hash_fn<Algorithm, H>,
                               typename Algorithm::key_type const &key,
                               std::basic_string_view<Char, Traits> const &str) noexcept
        {
            return Algorithm::template hash<H>(
                    key, reinterpret_cast<std::byte const *>(str.data()), str.size());
        }


Concepts
--------

.. concept:: template <typename T> \
             hash_algorithm

    A :concept:`hash_algorithm` is a type which implements a few functions 
    which map byte arrays to an integer values. The hashes obtained via these
    function must not diverge during program execution.

    :expr:`T` must satisfy `std::semiregular <https://en.cppreference.com/w/cpp/concepts/semiregular>`_,
    i.e. it must be default initializable and copyable.

    All functions are required to be ``noexcept``.

    **Notation**

    .. var:: T &state

        The state for streaming hash operation.

    .. var:: std::byte const *data

        A pointer to a byte array to be hashed.

    .. var:: std::size_t byteSize

        The size (in bytes) of the :texpr:`data` byte array.

    **Valid Expressions**

    - :expr:`T::hash<std::uint32_t>(data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its 32bit hash.
    - :expr:`T::hash<std::uint64_t>(data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its 64bit hash.
    - :expr:`T::hash<std::size_t>(data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its machine word sized hash.

    - :expr:`state.update(data, byteSize)` ingests the byte array referenced by
      :texpr:`data`.
    - :expr:`state.final<std::uint32_t>()` returns the 32bit hash of all 
      previously ingested bytes. The instance should be considered tainted after
      this call and should therefore be reinitialized before it is reused.
    - :expr:`state.final<std::uint64_t>()` returns the 64bit hash of all 
      previously ingested bytes. The instance should be considered tainted after
      this call and should therefore be reinitialized before it is reused.

.. concept:: template <typename T> \
             keyable_hash_algorithm

    A type which satisfies :concept:`hash_algorithm` and additionally supports
    generating different hashes for the same input bytes by supplying an
    additional key parameter.

    **Notation**

    .. var:: typename T::key_type const &key

        A value with which the algorithm can be keyed.

    .. var:: std::span<typename T::key_type> const &keys

        A contiguous range of algorithm keys.

    .. var:: std::byte const *data

        A pointer to a byte array to be hashed.

    .. var:: std::size_t byteSize

        The size (in bytes) of the :expr:`data` byte array.

    **Valid Expressions**

    - :expr:`T(key)` constructs a hash state from a given key.
    - :expr:`T::generate_key()` generates a random key and returns it.
    - :expr:`T::generate_keys(keys)` generates many random keys and stores them
      in the range provided by the caller.
    - :expr:`T::hash<std::uint32_t>(key, data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its 32bit hash keyed by :texpr:`key`.
    - :expr:`T::hash<std::uint64_t>(key, data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its 64bit hash keyed by :texpr:`key`.
    - :expr:`T::hash<std::size_t>(key, data, byteSize)` hashes the byte array 
      referenced by :texpr:`data` and returns its machine word sized hash keyed 
      by :texpr:`key`.

.. concept:: template<typename T> \
             trivially_hashable
    
    Identifies types which can be hashed by hashing its byte representation with
    can be explicitly opted out with the :texpr:`disable_trivially_hashable` switch.

    .. seealso:: 

        `std::has_unique_object_representations <https://en.cppreference.com/w/cpp/types/has_unique_object_representations>`_
            The standard trait which forms the basis of this concept.

.. var::  template <typename T> \
          inline constexpr bool disable_trivially_hashable



Hash Algorithms
---------------

.. class:: spooky_v2_hash

    ::

        #include <vefs/hash/spooky_v2.hpp>

    Implements the SpookyHash V2 algorithm by Bob Jenkins for the :concept:`keyable_hash_algorithm`
    concept.

    .. seealso:: `Bob Jenkins Website <https://burtleburtle.net/bob/hash/spooky.html>`_


Specializations for standard Types
----------------------------------

:: 

    #include <vefs/hash/hash-std.hpp>

- :expr:`std::basic_string`
- :expr:`std::basic_string_view`

