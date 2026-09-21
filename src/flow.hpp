#pragma once

#include <array>
#include <string_view>
#include <cstdint>

namespace flow
{

enum class Node : uint8_t {
    kB13,
    kB12,
    kB11,
    kB10,
    kB9b,
    kB8,
    kB7,
    kB6,
    kB5,
    kB4,
    kB3,
    kC3,
    kC4,
    kD4,
    kD5,
    kF6,
    kF7,
    kG7,
    kG8,
    kG9,
    kG11,
    kH7,
    kH10,
    kH11,
    kH12,
    kI4,
    kI7,
    kI12,
    kI13,
    kJ18,
    kK5,
    kK7,
    kK13,
    kL5,
    kL7,
    kL13,
    kL14,
    kL17,
    kM5,
    kM7,
    kM16,
    kM20,
    kM20b,
    kN5,
    kN11,
    kN16,
    kO14,
    kO16,
    kO18,
    kO18b,
    kO18c,
    kO18d,
    kO18e,
    kO20,
    kP3,
    kP11,
    kCount,
};

enum class Kind : uint8_t { kRequest, kResource, kConneg, kAction };

struct Target {
    Node node;
    uint16_t status;
};

constexpr Target to(const Node node)
{
    return Target{node, 0};
}

constexpr Target halt(const uint16_t status)
{
    return Target{Node::kCount, status};
}

struct FlowNode {
    Node id;
    Kind kind;
    std::string_view callback;
    std::string_view clause;
    Target on_true;
    Target on_false;
};

inline constexpr std::array<std::string_view, static_cast<size_t>(Node::kCount) + 1> kNames = {{
    "B13", "B12",  "B11",  "B10",  "B9b",  "B8",  "B7",   "B6",  "B5",  "B4",  "B3",  "C3",
    "C4",  "D4",   "D5",   "F6",   "F7",   "G7",  "G8",   "G9",  "G11", "H7",  "H10", "H11",
    "H12", "I4",   "I7",   "I12",  "I13",  "J18", "K5",   "K7",  "K13", "L5",  "L7",  "L13",
    "L14", "L17",  "M5",   "M7",   "M16",  "M20", "M20b", "N5",  "N11", "N16", "O14", "O16",
    "O18", "O18b", "O18c", "O18d", "O18e", "O20", "P3",   "P11", "",
}};

constexpr std::string_view name_of(const Node id)
{
    return kNames.at(static_cast<size_t>(id));
}

inline constexpr std::array<FlowNode, static_cast<size_t>(Node::kCount)> kFlow = {{
    {Node::kB13, Kind::kResource, "service_available?", "RFC 9110 15.6.4 (503)", to(Node::kB12),
     halt(503)},
    {Node::kB12, Kind::kResource, "known_methods", "RFC 9110 15.6.2 (501); list x method",
     to(Node::kB11), halt(501)},
    {Node::kB11, Kind::kResource, "uri_too_long?", "RFC 9110 15.5.15 (414)", halt(414),
     to(Node::kB10)},
    {Node::kB10, Kind::kResource, "allowed_methods", "RFC 9110 15.5.6 (405) + 10.2.1 Allow",
     to(Node::kB9b), halt(405)},
    {Node::kB9b, Kind::kResource, "malformed_request?", "RFC 9110 15.5.1 (400)", halt(400),
     to(Node::kB8)},
    {Node::kB8, Kind::kResource, "is_authorized?",
     "RFC 9110 15.5.2 (401) + 11.6.1 WWW-Authenticate", to(Node::kB7), halt(401)},
    {Node::kB7, Kind::kResource, "forbidden?", "RFC 9110 15.5.4 (403)", halt(403), to(Node::kB6)},
    {Node::kB6, Kind::kResource, "valid_content_headers?", "RFC 9110 15.6.2 (501); Content-* set",
     to(Node::kB5), halt(501)},
    {Node::kB5, Kind::kResource, "known_content_type?", "RFC 9110 15.5.16 (415)", to(Node::kB4),
     halt(415)},
    {Node::kB4, Kind::kResource, "valid_entity_length?", "RFC 9110 15.5.14 (413)", to(Node::kB3),
     halt(413)},
    {Node::kB3, Kind::kRequest, "options", "RFC 9110 9.3.7: OPTIONS answers 200 from options()",
     halt(200), to(Node::kC3)},
    {Node::kC3, Kind::kRequest, "content_types_provided",
     "RFC 9110 12.5.1: Accept absent takes the first provided type", to(Node::kC4), to(Node::kD4)},
    {Node::kC4, Kind::kConneg, "content_types_provided", "RFC 9110 12.5.1 / 15.5.7 (406)",
     to(Node::kD4), halt(406)},
    {Node::kD4, Kind::kRequest, "languages_provided",
     "RFC 9110 12.5.4: absent negotiates '*' and may still 406", to(Node::kD5), to(Node::kF6)},
    {Node::kD5, Kind::kConneg, "languages_provided", "RFC 9110 12.5.4 / 15.5.7 (406)",
     to(Node::kF6), halt(406)},
    {Node::kF6, Kind::kRequest, "encodings_provided",
     "RFC 9110 12.5.3: absent negotiates identity;q=1,*;q=0.5; Content-Type header lands here",
     to(Node::kF7), to(Node::kG7)},
    {Node::kF7, Kind::kConneg, "encodings_provided", "RFC 9110 12.5.3 / 15.5.7 (406)",
     to(Node::kG7), halt(406)},
    {Node::kG7, Kind::kResource, "resource_exists?", "RFC 9110 12.5.5: Vary lands here",
     to(Node::kG8), to(Node::kH7)},
    {Node::kG8,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match present?",
     to(Node::kG9),
     to(Node::kH10)},
    {Node::kG9,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match is '*'?; a true If-Match skips If-Unmodified-Since",
     to(Node::kI12),
     to(Node::kG11)},
    {Node::kG11, Kind::kResource, "generate_etag",
     "RFC 9110 13.1.1 / 15.5.13 (412); a true If-Match skips If-Unmodified-Since", to(Node::kI12),
     halt(412)},
    {Node::kH7,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match '*' against a missing resource is 412",
     halt(412),
     to(Node::kI7)},
    {Node::kH10,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.4: If-Unmodified-Since present?",
     to(Node::kH11),
     to(Node::kI12)},
    {Node::kH11,
     Kind::kRequest,
     {},
     "RFC 9110 5.6.7: IUS parses as HTTP-date?",
     to(Node::kH12),
     to(Node::kI12)},
    {Node::kH12, Kind::kResource, "last_modified", "RFC 9110 13.1.4 / 15.5.13 (412)", halt(412),
     to(Node::kI12)},
    {Node::kI4, Kind::kResource, "moved_permanently?", "RFC 9110 15.4.2 (301) + Location",
     halt(301), to(Node::kP3)},
    {Node::kI7, Kind::kRequest, {}, "RFC 9110 9.3.4: PUT?", to(Node::kI4), to(Node::kK7)},
    {Node::kI12,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: If-None-Match present?",
     to(Node::kI13),
     to(Node::kL13)},
    {Node::kI13,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: If-None-Match is '*'?",
     to(Node::kJ18),
     to(Node::kK13)},
    {Node::kJ18,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: GET/HEAD gets 304 (15.4.5), others 412 (15.5.13)",
     halt(304),
     halt(412)},
    {Node::kK5, Kind::kResource, "moved_permanently?", "RFC 9110 15.4.2 (301) + Location",
     halt(301), to(Node::kL5)},
    {Node::kK7, Kind::kResource, "previously_existed?", "RFC 9110 15.4/15.5: gone vs never",
     to(Node::kK5), to(Node::kL7)},
    {Node::kK13, Kind::kResource, "generate_etag",
     "RFC 9110 13.2.2: If-None-Match decides alone; If-Modified-Since is not asked", to(Node::kJ18),
     to(Node::kM16)},
    {Node::kL5, Kind::kResource, "moved_temporarily?", "RFC 9110 15.4.8 (307) + Location",
     halt(307), to(Node::kM5)},
    {Node::kL7,
     Kind::kRequest,
     {},
     "RFC 9110 15.5.5 (404): only POST may proceed",
     to(Node::kM7),
     halt(404)},
    {Node::kL13,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.3: If-Modified-Since present?",
     to(Node::kL14),
     to(Node::kM16)},
    {Node::kL14,
     Kind::kRequest,
     {},
     "RFC 9110 5.6.7: IMS parses as HTTP-date?",
     to(Node::kL17),
     to(Node::kM16)},
    {Node::kL17, Kind::kResource, "last_modified", "RFC 9110 13.1.3 / 15.4.5 (304)", to(Node::kM16),
     halt(304)},
    {Node::kM5,
     Kind::kRequest,
     {},
     "RFC 9110 15.5.11 (410): only POST may revive",
     to(Node::kN5),
     halt(410)},
    {Node::kM7, Kind::kResource, "allow_missing_post?", "RFC 9110 9.3.3 / 15.5.5 (404)",
     to(Node::kN11), halt(404)},
    {Node::kM16, Kind::kRequest, {}, "RFC 9110 9.3.5: DELETE?", to(Node::kM20), to(Node::kN16)},
    {Node::kM20, Kind::kAction, "delete_resource", "RFC 9110 9.3.5; false is 500 (15.6.1)",
     to(Node::kM20b), halt(500)},
    {Node::kM20b, Kind::kResource, "delete_completed?", "RFC 9110 15.3.3 (202) when async",
     to(Node::kO20), halt(202)},
    {Node::kN5, Kind::kResource, "allow_missing_post?", "RFC 9110 9.3.3 / 15.5.11 (410)",
     to(Node::kN11), halt(410)},
    {Node::kN11, Kind::kAction, "post_is_create?",
     "RFC 9110 9.3.3: create_path/base_uri or process_post; redirect is 303 (15.4.4)", halt(303),
     to(Node::kP11)},
    {Node::kN16, Kind::kRequest, {}, "RFC 9110 9.3.3: POST?", to(Node::kN11), to(Node::kO16)},
    {Node::kO14, Kind::kAction, "is_conflict?",
     "RFC 9110 15.5.10 (409); false runs content_types_accepted (accept_helper)", halt(409),
     to(Node::kP11)},
    {Node::kO16, Kind::kRequest, {}, "RFC 9110 9.3.4: PUT?", to(Node::kO14), to(Node::kO18)},
    {Node::kO18, Kind::kAction, "content_types_provided",
     "GET, HEAD and QUERY (RFC 10008) render the body through the negotiated handler; "
     "caching headers land here",
     to(Node::kO18c), to(Node::kO18c)},
    {Node::kO18c,
     Kind::kRequest,
     {},
     "RFC 9110 14.2: a Range on a GET - \"GET is the only method for which range handling is "
     "defined\" - in a unit this server serves; any other is ignored",
     to(Node::kO18d),
     to(Node::kO18b)},
    {Node::kO18d,
     Kind::kRequest,
     {},
     "RFC 9110 13.2.2 step 5 / 13.1.5: no If-Range, or its validator matches; otherwise the "
     "Range is ignored and the whole representation is sent",
     to(Node::kO18e),
     to(Node::kO18b)},
    {Node::kO18e, Kind::kResource, "complete_length",
     "RFC 9110 14.1.2: satisfiable is 206 (15.3.7), unsatisfiable is 416 (15.5.17)", halt(206),
     halt(416)},
    {Node::kO18b, Kind::kResource, "multiple_choices?", "RFC 9110 15.4.1 (300) / 15.3.1 (200)",
     halt(300), halt(200)},
    {Node::kO20,
     Kind::kRequest,
     {},
     "RFC 9110 15.3.5 (204): response carries no entity",
     to(Node::kO18),
     halt(204)},
    {Node::kP3, Kind::kAction, "is_conflict?",
     "RFC 9110 15.5.10 (409); false runs content_types_accepted (accept_helper)", halt(409),
     to(Node::kP11)},
    {Node::kP11,
     Kind::kRequest,
     {},
     "RFC 9110 15.3.2 (201): Location was set",
     halt(201),
     to(Node::kO20)},
}};

constexpr const FlowNode &node_of(const Node id)
{
    return kFlow.at(static_cast<size_t>(id));
}

constexpr Target next(const Node id, const bool answer)
{
    return answer ? node_of(id).on_true : node_of(id).on_false;
}

} // namespace flow
