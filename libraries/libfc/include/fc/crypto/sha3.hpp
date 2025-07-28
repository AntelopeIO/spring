#pragma once
#include <fc/fwd.hpp>
#include <fc/string.hpp>
#include <fc/platform_independence.hpp>
#include <fc/crypto/packhash.hpp>
#include <fc/io/raw_fwd.hpp>
#include <boost/functional/hash.hpp>

namespace fc
{

class sha3
{
public:
	sha3();
	~sha3(){}
	explicit sha3(const std::string &hex_str);
	explicit sha3(const char *data, size_t size);

	std::string str() const;
	operator std::string() const;

	const char *data() const;
	char* data();
	size_t data_size() const { return 256 / 8; }

	static sha3 hash(const char *d, uint32_t dlen, bool is_nist=true) {
		encoder e;
		e.write(d, dlen);
		const auto& sha = e.result(is_nist);
		return sha;
	}
	static sha3 hash(const std::string& s, bool is_nist=true) { return hash(s.c_str(), s.size(), is_nist); }
	static sha3 hash(const sha3& s, bool is_nist=true) { return hash(s.data(), sizeof(s._hash), is_nist); }

	class encoder
	{
	public:
		encoder();
		~encoder();

		void write(const char *d, uint32_t dlen);
		void put(char c) { write(&c, 1); }
		void reset();
		sha3 result(bool is_nist=true);

	private:
		struct impl;
		fc::fwd<impl, 1016> my;
	};

	struct keccak {
		struct encoder_t : public encoder {
			sha3 result() { return encoder::result(true); }
		};
	};
	struct nist {
		struct encoder_t : public encoder {
			sha3 result() { return encoder::result(false); }
		};
	};

	template <typename Algo>
	static constexpr bool is_sha3_algo_v = std::is_same_v<Algo, keccak> || std::is_same_v<Algo, nist>;

	template <typename SHA3Algo, typename T>
	requires is_sha3_algo_v<SHA3Algo>
	static sha3 hash(SHA3Algo, const T &t) {
		return packhash(SHA3Algo{}, t);
	}

	template <typename SHA3Algo, typename... T>
	requires (is_sha3_algo_v<SHA3Algo> && sizeof...(T) > 0)
	static sha3 packhash(SHA3Algo, const T&... t) {
		return packhash<SHA3Algo::encoder_t>(t...);
	}

	template <typename T>
	inline friend T &operator<<(T &ds, const sha3 &ep)
	{
		ds.write(ep.data(), sizeof(ep));
		return ds;
	}

	template <typename T>
	inline friend T &operator>>(T &ds, sha3 &ep)
	{
		ds.read(ep.data(), sizeof(ep));
		return ds;
	}
	friend sha3 operator<<(const sha3 &h1, uint32_t i);
	friend sha3 operator>>(const sha3 &h1, uint32_t i);
	friend bool operator==(const sha3 &h1, const sha3 &h2);
	friend bool operator!=(const sha3 &h1, const sha3 &h2);
	friend sha3 operator^(const sha3 &h1, const sha3 &h2);
	friend bool operator>=(const sha3 &h1, const sha3 &h2);
	friend bool operator>(const sha3 &h1, const sha3 &h2);
	friend bool operator<(const sha3 &h1, const sha3 &h2);

	uint64_t _hash[4];
};

class variant;
void to_variant(const sha3 &bi, variant &v);
void from_variant(const variant &v, sha3 &bi);

} // namespace fc

namespace std
{
template <>
struct hash<fc::sha3>
{
	size_t operator()(const fc::sha3 &s) const
	{
		return *((size_t *)&s);
	}
};

} // namespace std

namespace boost
{
template <>
struct hash<fc::sha3>
{
	size_t operator()(const fc::sha3 &s) const
	{
		return s._hash[3]; //*((size_t*)&s);
	}
};
} // namespace boost
#include <fc/reflect/reflect.hpp>
FC_REFLECT_TYPENAME(fc::sha3)
