#include <silkworm/core/rlp/encode.hpp>
#include <silkworm/core/rlp/decode.hpp>
#include <silkworm/core/types/evmc_bytes32.hpp>
#include <eosevm/block_extra_data.hpp>

using namespace silkworm;

namespace silkworm { namespace rlp {

    template <typename T>
    void encode(Bytes& to, const std::optional<T>& v) noexcept {
        auto has_value = v.has_value();
        rlp::encode(to, has_value);
        if(has_value) {
            rlp::encode(to, v.value());
        }
    }

    template <typename T>
    DecodingResult decode(ByteView& from, std::optional<T>& out) noexcept {
        bool has_value = false;
        if (DecodingResult res{decode(from, has_value, Leftover::kAllow)}; !res) {
            return res;
        }
        if(has_value) {
            out = T{};
            if (DecodingResult res{decode(from, *out, Leftover::kAllow)}; !res) {
                return res;
            }
        }
        return {};
    }

    silkworm::Bytes encode(const eosevm::block_extra_data& out) {
        silkworm::Bytes to;
        encode(to, out.consensus_parameter_index);
        encode(to, out.gasprices);
        return to;
    }

    DecodingResult decode(silkworm::ByteView& from, eosevm::block_extra_data& to) noexcept{
        to.consensus_parameter_index.reset();
        decode(from, to.consensus_parameter_index);
        if(from.length() > 0) {
            to.gasprices.reset();
            decode(from, to.gasprices);
        }
        return {};
    }

} }  //namespace silkworm::rlp