#pragma once

#include "block/body/body_fwd.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/merkle_leaves.hpp"
#include "general/reader.hpp"
#include "structured_reader_fwd.hpp"
#include <vector>

// The ParseNode class is used for representation and construction of
// structured description of parsing from binary. It is used applied
// in API functions to help understand the binary structure of Warthog
// blocks.
struct ParseAnnotation {
    ParseAnnotation(std::string name, size_t offsetBegin)
        : tag(std::move(name))
        , offsetBegin(offsetBegin)
    {
    }
    using Children = std::vector<ParseAnnotation>;

    // members
    std::string tag;
    size_t offsetBegin;
    size_t offsetEnd;
    wrt::optional<Children> children;
};
using ParseAnnotations = ParseAnnotation::Children;

// Reader with stack-like frame types that construct a
// human-readable tree of parsed sections with annotated meaning.

struct StructuredReader : public Reader {
private:
    struct Frame {
        StructuredReader& reader;

        template <typename T>
        operator T()
        {
            return T(reader);
        }
        operator Reader&()
        {
            return reader;
        }
        operator StructuredReader&()
        {
            return reader;
        }
        StructuredReader& operator->()
        {
            return reader;
        }
    };
    struct MerkleFrame : public Frame {
    public:
        MerkleFrame(const MerkleFrame& mi) = delete;
        MerkleFrame(MerkleFrame&& mi)
            : Frame(std::move(mi))
            , begin(mi.begin)
        {
            mi.begin = nullptr;
        }

        MerkleFrame(StructuredReader& c)
            : Frame(c)
            , begin(reader.cursor())
        {
        }

        ~MerkleFrame()
        {
            if (begin)
                reader.add_hash_of({ begin, reader.cursor() });
        }

    private:
        const uint8_t* begin;
    };

    // Sets offsetEnd in destructor
    struct AnnotatorFrame : public Frame {
        AnnotatorFrame(StructuredReader& r)
            : Frame(r)
        {
            annotations = reader.annotations; // take snapshot of annotations pointer (to be restored in destructor)
        }

        AnnotatorFrame(AnnotatorFrame&& other)
            : Frame(std::move(other))
            , annotations(other.annotations)
        {
            other.annotations = nullptr;
        }
        ~AnnotatorFrame()
        {
            if (annotations) {
                reader.annotations = annotations; // restore original children pointer (POP stack)
                annotations->back().offsetEnd = reader.offset(); // set offsetEnd
                reader.pendingAnnotation = false;
            }
        }

    private:
        ParseAnnotations* annotations;
    };

public:
    StructuredReader(Reader r, ParseAnnotations* annotations = nullptr)
        : Reader(std::move(r))
        , annotations(annotations)
    {
    }

    [[nodiscard]] AnnotatorFrame annotate(std::string name)
    {
        if (annotations) {
            if (pendingAnnotation) {
                annotations->back().children.emplace();
                annotations = &*annotations->back().children;
            }
            annotations->emplace_back(std::move(name), offset());
            pendingAnnotation = true;
        }
        return { *this };
    }

    [[nodiscard]] MerkleFrame merkle_frame() { return { *this }; }
    MerkleLeaves move_leaves() && { return std::move(leaves); }

private: // methods
    StructuredReader(const StructuredReader&) = delete;

    void add_hash_of(const std::span<const uint8_t>& s)
    {
        leaves.add_hash(hashSHA256(s));
    }

private:
    MerkleLeaves leaves;
    ParseAnnotations* annotations;
    bool pendingAnnotation { false };
};

template <StaticString annotation, typename T>
class Tag : public T {
public:
    using T::T;
    // Tag(T t):T(std::move(t)){}
    template <typename... Args>
    Tag(StructuredReader& s, Args&&... args)
        : T(s.annotate(annotation.to_string()).reader, std::forward<Args>(args)...)
    {
    }
    using parent_t = Tag;
};
