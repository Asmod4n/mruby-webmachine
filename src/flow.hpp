#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

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

enum class Question : uint8_t {
    kMethod,
    kContentLengthValue,
    kContentTypeMediaType,
    kContentHeadersAreValid,
    kAuthorizationCredentials,
    kAcceptIsPresent,
    kAcceptMediaTypes,
    kAcceptLanguageIsPresent,
    kAcceptLanguageTags,
    kAcceptEncodingIsPresent,
    kAcceptEncodingCodings,
    kIfMatchIsPresent,
    kIfMatchIsStar,
    kIfMatchEntityTags,
    kIfUnmodifiedSinceIsPresent,
    kIfUnmodifiedSinceIsDate,
    kIfUnmodifiedSinceMoment,
    kIfNoneMatchIsPresent,
    kIfNoneMatchIsStar,
    kIfNoneMatchEntityTags,
    kIfModifiedSinceIsPresent,
    kIfModifiedSinceIsDate,
    kIfModifiedSinceMoment,
    kRangeIsPresent,
    kRangeSpecifier,
    kIfRangeValidator,
    kCount,
};

using QuestionSet = uint32_t;

constexpr QuestionSet questions_asked()
{
    return 0;
}

template <class... Asked> constexpr QuestionSet questions_asked(const Asked... asked)
{
    return static_cast<QuestionSet>((... | (1U << static_cast<uint8_t>(asked))));
}

struct FlowNode {
    Node id;
    Kind kind;
    std::string_view callback;
    std::string_view clause;
    Target on_true;
    Target on_false;
    QuestionSet questions_asked;
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
     halt(503), questions_asked()},
    {Node::kB12, Kind::kResource, "known_methods", "RFC 9110 15.6.2 (501); list x method",
     to(Node::kB11), halt(501), questions_asked()},
    {Node::kB11, Kind::kResource, "uri_too_long?", "RFC 9110 15.5.15 (414)", halt(414),
     to(Node::kB10), questions_asked()},
    {Node::kB10, Kind::kResource, "allowed_methods", "RFC 9110 15.5.6 (405) + 10.2.1 Allow",
     to(Node::kB9b), halt(405), questions_asked()},
    {Node::kB9b, Kind::kResource, "malformed_request?", "RFC 9110 15.5.1 (400)", halt(400),
     to(Node::kB8), questions_asked()},
    {Node::kB8, Kind::kResource, "is_authorized?",
     "RFC 9110 15.5.2 (401) + 11.6.1 WWW-Authenticate", to(Node::kB7), halt(401),
     questions_asked(Question::kAuthorizationCredentials)},
    {Node::kB7, Kind::kResource, "forbidden?", "RFC 9110 15.5.4 (403)", halt(403), to(Node::kB6),
     questions_asked()},
    {Node::kB6, Kind::kResource, "valid_content_headers?", "RFC 9110 15.6.2 (501); Content-* set",
     to(Node::kB5), halt(501), questions_asked(Question::kContentHeadersAreValid)},
    {Node::kB5, Kind::kResource, "known_content_type?", "RFC 9110 15.5.16 (415)", to(Node::kB4),
     halt(415), questions_asked(Question::kContentTypeMediaType)},
    {Node::kB4, Kind::kResource, "valid_entity_length?", "RFC 9110 15.5.14 (413)", to(Node::kB3),
     halt(413), questions_asked(Question::kContentLengthValue)},
    {Node::kB3, Kind::kRequest, "options", "RFC 9110 9.3.7: OPTIONS answers 200 from options()",
     halt(200), to(Node::kC3), questions_asked(Question::kMethod)},
    {Node::kC3, Kind::kRequest, "content_types_provided",
     "RFC 9110 12.5.1: Accept absent takes the first provided type", to(Node::kC4), to(Node::kD4),
     questions_asked(Question::kAcceptIsPresent)},
    {Node::kC4, Kind::kConneg, "content_types_provided", "RFC 9110 12.5.1 / 15.5.7 (406)",
     to(Node::kD4), halt(406), questions_asked(Question::kAcceptMediaTypes)},
    {Node::kD4, Kind::kRequest, "languages_provided",
     "RFC 9110 12.5.4: absent negotiates '*' and may still 406", to(Node::kD5), to(Node::kF6),
     questions_asked(Question::kAcceptLanguageIsPresent)},
    {Node::kD5, Kind::kConneg, "languages_provided", "RFC 9110 12.5.4 / 15.5.7 (406)",
     to(Node::kF6), halt(406), questions_asked(Question::kAcceptLanguageTags)},
    {Node::kF6, Kind::kRequest, "encodings_provided",
     "RFC 9110 12.5.3: absent negotiates identity;q=1,*;q=0.5; Content-Type header lands here",
     to(Node::kF7), to(Node::kG7), questions_asked(Question::kAcceptEncodingIsPresent)},
    {Node::kF7, Kind::kConneg, "encodings_provided", "RFC 9110 12.5.3 / 15.5.7 (406)",
     to(Node::kG7), halt(406), questions_asked(Question::kAcceptEncodingCodings)},
    {Node::kG7, Kind::kResource, "resource_exists?", "RFC 9110 12.5.5: Vary lands here",
     to(Node::kG8), to(Node::kH7), questions_asked()},
    {Node::kG8,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match present?",
     to(Node::kG9),
     to(Node::kH10),
     questions_asked(Question::kIfMatchIsPresent)},
    {Node::kG9,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match is '*'?; a true If-Match skips If-Unmodified-Since",
     to(Node::kI12),
     to(Node::kG11),
     questions_asked(Question::kIfMatchIsStar)},
    {Node::kG11, Kind::kResource, "generate_etag",
     "RFC 9110 13.1.1 / 15.5.13 (412); a true If-Match skips If-Unmodified-Since", to(Node::kI12),
     halt(412), questions_asked(Question::kIfMatchEntityTags)},
    {Node::kH7,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.1: If-Match '*' against a missing resource is 412",
     halt(412),
     to(Node::kI7),
     questions_asked(Question::kIfMatchIsStar)},
    {Node::kH10,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.4: If-Unmodified-Since present?",
     to(Node::kH11),
     to(Node::kI12),
     questions_asked(Question::kIfUnmodifiedSinceIsPresent)},
    {Node::kH11,
     Kind::kRequest,
     {},
     "RFC 9110 5.6.7: IUS parses as HTTP-date?",
     to(Node::kH12),
     to(Node::kI12),
     questions_asked(Question::kIfUnmodifiedSinceIsDate)},
    {Node::kH12, Kind::kResource, "last_modified", "RFC 9110 13.1.4 / 15.5.13 (412)", halt(412),
     to(Node::kI12), questions_asked(Question::kIfUnmodifiedSinceMoment)},
    {Node::kI4, Kind::kResource, "moved_permanently?", "RFC 9110 15.4.2 (301) + Location",
     halt(301), to(Node::kP3), questions_asked()},
    {Node::kI7,
     Kind::kRequest,
     {},
     "RFC 9110 9.3.4: PUT?",
     to(Node::kI4),
     to(Node::kK7),
     questions_asked(Question::kMethod)},
    {Node::kI12,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: If-None-Match present?",
     to(Node::kI13),
     to(Node::kL13),
     questions_asked(Question::kIfNoneMatchIsPresent)},
    {Node::kI13,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: If-None-Match is '*'?",
     to(Node::kJ18),
     to(Node::kK13),
     questions_asked(Question::kIfNoneMatchIsStar)},
    {Node::kJ18,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.2: GET/HEAD gets 304 (15.4.5), others 412 (15.5.13)",
     halt(304),
     halt(412),
     questions_asked(Question::kMethod, Question::kIfNoneMatchEntityTags)},
    {Node::kK5, Kind::kResource, "moved_permanently?", "RFC 9110 15.4.2 (301) + Location",
     halt(301), to(Node::kL5), questions_asked()},
    {Node::kK7, Kind::kResource, "previously_existed?", "RFC 9110 15.4/15.5: gone vs never",
     to(Node::kK5), to(Node::kL7), questions_asked()},
    {Node::kK13, Kind::kResource, "generate_etag",
     "RFC 9110 13.2.2: If-None-Match decides alone; If-Modified-Since is not asked", to(Node::kJ18),
     to(Node::kM16), questions_asked(Question::kIfNoneMatchEntityTags)},
    {Node::kL5, Kind::kResource, "moved_temporarily?", "RFC 9110 15.4.8 (307) + Location",
     halt(307), to(Node::kM5), questions_asked()},
    {Node::kL7,
     Kind::kRequest,
     {},
     "RFC 9110 15.5.5 (404): only POST may proceed",
     to(Node::kM7),
     halt(404),
     questions_asked(Question::kMethod)},
    {Node::kL13,
     Kind::kRequest,
     {},
     "RFC 9110 13.1.3: If-Modified-Since present?",
     to(Node::kL14),
     to(Node::kM16),
     questions_asked(Question::kIfModifiedSinceIsPresent)},
    {Node::kL14,
     Kind::kRequest,
     {},
     "RFC 9110 5.6.7: IMS parses as HTTP-date?",
     to(Node::kL17),
     to(Node::kM16),
     questions_asked(Question::kIfModifiedSinceIsDate)},
    {Node::kL17, Kind::kResource, "last_modified", "RFC 9110 13.1.3 / 15.4.5 (304)", to(Node::kM16),
     halt(304), questions_asked(Question::kIfModifiedSinceMoment)},
    {Node::kM5,
     Kind::kRequest,
     {},
     "RFC 9110 15.5.11 (410): only POST may revive",
     to(Node::kN5),
     halt(410),
     questions_asked(Question::kMethod)},
    {Node::kM7, Kind::kResource, "allow_missing_post?", "RFC 9110 9.3.3 / 15.5.5 (404)",
     to(Node::kN11), halt(404), questions_asked()},
    {Node::kM16,
     Kind::kRequest,
     {},
     "RFC 9110 9.3.5: DELETE?",
     to(Node::kM20),
     to(Node::kN16),
     questions_asked(Question::kMethod)},
    {Node::kM20, Kind::kAction, "delete_resource", "RFC 9110 9.3.5; false is 500 (15.6.1)",
     to(Node::kM20b), halt(500), questions_asked()},
    {Node::kM20b, Kind::kResource, "delete_completed?", "RFC 9110 15.3.3 (202) when async",
     to(Node::kO20), halt(202), questions_asked()},
    {Node::kN5, Kind::kResource, "allow_missing_post?", "RFC 9110 9.3.3 / 15.5.11 (410)",
     to(Node::kN11), halt(410), questions_asked()},
    {Node::kN11, Kind::kAction, "post_is_create?",
     "RFC 9110 9.3.3: create_path/base_uri or process_post; redirect is 303 (15.4.4)", halt(303),
     to(Node::kP11), questions_asked()},
    {Node::kN16,
     Kind::kRequest,
     {},
     "RFC 9110 9.3.3: POST?",
     to(Node::kN11),
     to(Node::kO16),
     questions_asked(Question::kMethod)},
    {Node::kO14, Kind::kAction, "is_conflict?",
     "RFC 9110 15.5.10 (409); false runs content_types_accepted (accept_helper)", halt(409),
     to(Node::kP11), questions_asked()},
    {Node::kO16,
     Kind::kRequest,
     {},
     "RFC 9110 9.3.4: PUT?",
     to(Node::kO14),
     to(Node::kO18),
     questions_asked(Question::kMethod)},
    {Node::kO18, Kind::kAction, "content_types_provided",
     "GET, HEAD and QUERY (RFC 10008) render the body through the negotiated handler; "
     "caching headers land here",
     to(Node::kO18c), to(Node::kO18c), questions_asked()},
    {Node::kO18b, Kind::kResource, "multiple_choices?", "RFC 9110 15.4.1 (300) / 15.3.1 (200)",
     halt(300), halt(200), questions_asked()},
    {Node::kO18c,
     Kind::kRequest,
     {},
     "RFC 9110 14.2: a Range on a GET - \"GET is the only method for which range handling is "
     "defined\" - in a unit this server serves; any other is ignored",
     to(Node::kO18d),
     to(Node::kO18b),
     questions_asked(Question::kRangeIsPresent)},
    {Node::kO18d,
     Kind::kRequest,
     {},
     "RFC 9110 13.2.2 step 5 / 13.1.5: no If-Range, or its validator matches; otherwise the "
     "Range is ignored and the whole representation is sent",
     to(Node::kO18e),
     to(Node::kO18b),
     questions_asked(Question::kIfRangeValidator)},
    {Node::kO18e, Kind::kResource, "complete_length",
     "RFC 9110 14.1.2: satisfiable is 206 (15.3.7), unsatisfiable is 416 (15.5.17)", halt(206),
     halt(416), questions_asked(Question::kRangeSpecifier)},
    {Node::kO20,
     Kind::kRequest,
     {},
     "RFC 9110 15.3.5 (204): response carries no entity",
     to(Node::kO18),
     halt(204),
     questions_asked(Question::kMethod)},
    {Node::kP3, Kind::kAction, "is_conflict?",
     "RFC 9110 15.5.10 (409); false runs content_types_accepted (accept_helper)", halt(409),
     to(Node::kP11), questions_asked()},
    {Node::kP11,
     Kind::kRequest,
     {},
     "RFC 9110 15.3.2 (201): Location was set",
     halt(201),
     to(Node::kO20),
     questions_asked()},
}};

constexpr const FlowNode &node_of(const Node id)
{
    return kFlow.at(static_cast<size_t>(id));
}

constexpr Target next(const Node id, const bool answer)
{
    return answer ? node_of(id).on_true : node_of(id).on_false;
}

inline constexpr size_t kNodeCount = static_cast<size_t>(Node::kCount);

constexpr bool every_row_stands_at_its_own_index()
{
    for (size_t at = 0; at < kNodeCount; at++)
        if (kFlow.at(at).id != static_cast<Node>(at))
            return false;
    return true;
}

static_assert(every_row_stands_at_its_own_index(), "every row stands at its own index");

constexpr bool every_edge_continues_or_halts()
{
    for (const FlowNode &node : kFlow)
        for (const Target target : {node.on_true, node.on_false})
            if ((target.status == 0) == (target.node == Node::kCount))
                return false;
    return true;
}

static_assert(every_edge_continues_or_halts(), "every edge continues or halts");

enum class Colour : uint8_t { kUnseen, kOnThePath, kFinished };

constexpr bool no_cycle_from(const Node id, std::array<Colour, kNodeCount> &colour)
{
    Colour &here = colour.at(static_cast<size_t>(id));
    if (here == Colour::kOnThePath) [[unlikely]]
        return false;
    if (here == Colour::kFinished)
        return true;
    here = Colour::kOnThePath;
    for (const Target target : {node_of(id).on_true, node_of(id).on_false})
        if (target.status == 0 && !no_cycle_from(target.node, colour))
            return false;
    here = Colour::kFinished;
    return true;
}

constexpr bool the_flow_is_acyclic()
{
    std::array<Colour, kNodeCount> colour{};
    return no_cycle_from(Node::kB13, colour);
}

static_assert(the_flow_is_acyclic(), "the flow is acyclic from B13");

constexpr void mark_reachable(const Node id, std::array<bool, kNodeCount> &seen)
{
    bool &here = seen.at(static_cast<size_t>(id));
    if (here)
        return;
    here = true;
    for (const Target target : {node_of(id).on_true, node_of(id).on_false})
        if (target.status == 0)
            mark_reachable(target.node, seen);
}

constexpr bool every_node_is_reachable()
{
    std::array<bool, kNodeCount> seen{};
    mark_reachable(Node::kB13, seen);
    return std::ranges::all_of(seen, [](const bool one) { return one; });
}

static_assert(every_node_is_reachable(), "every node is reachable from B13");

constexpr QuestionSet questions_reachable_from(const Node id, std::array<bool, kNodeCount> &seen)
{
    bool &here = seen.at(static_cast<size_t>(id));
    if (here)
        return 0;
    here = true;
    QuestionSet asked = node_of(id).questions_asked;
    for (const Target target : {node_of(id).on_true, node_of(id).on_false})
        if (target.status == 0)
            asked |= questions_reachable_from(target.node, seen);
    return asked;
}

constexpr QuestionSet questions_the_walk_can_ask(const Node from)
{
    std::array<bool, kNodeCount> seen{};
    return questions_reachable_from(from, seen);
}

inline constexpr QuestionSet kEveryQuestion = questions_the_walk_can_ask(Node::kB13);

constexpr QuestionSet every_question_there_is()
{
    QuestionSet all = 0;
    for (uint8_t which = 0; which < static_cast<uint8_t>(Question::kCount); which++)
        all |= static_cast<QuestionSet>(1U) << which;
    return all;
}

static_assert(kEveryQuestion == every_question_there_is(),
              "every question the flow declares is asked somewhere");

constexpr bool goes_to(const Node from, const bool answer, const Node arrive)
{
    const Target target = next(from, answer);
    return target.status == 0 && target.node == arrive;
}

constexpr bool halts_with(const Node from, const bool answer, const uint16_t status)
{
    return next(from, answer).status == status;
}

constexpr bool every_node_names_its_clause()
{
    for (const FlowNode &node : kFlow)
        if (node.clause.find("RFC") == std::string_view::npos &&
            node.clause.find("GET/HEAD") == std::string_view::npos)
            return false;
    return true;
}

static_assert(every_node_names_its_clause(), "every node names the clause it implements");

static_assert(goes_to(Node::kG9, true, Node::kI12) && goes_to(Node::kG11, true, Node::kI12) &&
                  goes_to(Node::kG8, false, Node::kH10),
              "a satisfied If-Match does not ask If-Unmodified-Since");

static_assert(goes_to(Node::kK13, false, Node::kM16) && goes_to(Node::kI12, false, Node::kL13),
              "an If-None-Match that is present decides alone");

static_assert(goes_to(Node::kD5, true, Node::kF6) && goes_to(Node::kD4, false, Node::kF6),
              "the charset nodes are gone");

static_assert(goes_to(Node::kL14, true, Node::kL17),
              "no node reads a date in the future as a rule of its own");

static_assert(goes_to(Node::kM16, false, Node::kN16) && goes_to(Node::kN16, false, Node::kO16) &&
                  goes_to(Node::kO16, false, Node::kO18),
              "a method that is neither DELETE nor POST nor PUT walks M16 to O18");

static_assert(goes_to(Node::kO18c, false, Node::kO18b) &&
                  goes_to(Node::kO18d, false, Node::kO18b),
              "a method that is not GET leaves the range branch");

static_assert(goes_to(Node::kO18, true, Node::kO18c) && goes_to(Node::kO18, false, Node::kO18c) &&
                  goes_to(Node::kO18c, true, Node::kO18d) &&
                  goes_to(Node::kO18d, true, Node::kO18e) && halts_with(Node::kO18e, true, 206) &&
                  halts_with(Node::kO18e, false, 416) && halts_with(Node::kO18b, true, 300) &&
                  halts_with(Node::kO18b, false, 200),
              "the graph answers a range request");

static_assert(halts_with(Node::kB13, false, 503) && halts_with(Node::kB12, false, 501) &&
                  halts_with(Node::kB10, false, 405) && halts_with(Node::kB5, false, 415) &&
                  halts_with(Node::kG11, false, 412) && halts_with(Node::kJ18, true, 304) &&
                  halts_with(Node::kJ18, false, 412) && halts_with(Node::kL7, false, 404) &&
                  halts_with(Node::kM5, false, 410) && halts_with(Node::kP11, true, 201) &&
                  halts_with(Node::kO20, false, 204),
              "the graph still answers the statuses webmachine answers");

}
