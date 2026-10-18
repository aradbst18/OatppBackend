#include "app/infrastructure/database/OrmSupport.hpp"

#include <utility>

#include "app/errors/AppException.hpp"

namespace app::infrastructure::database
{

namespace
{

void throwColonReserved(const std::string& sql)
{
    throw errors::database::QueryFailed(
        "SQL may contain ':' only inside single-quoted literals: the oatpp ORM parsers reserve it "
        "for named parameters "
        "(use cast(x as type) instead of a PostgreSQL '::' cast). Statement: " +
        sql);
}

}  // namespace

OrmQuery toOrmQuery(const std::string& sql)
{
    OrmQuery query;
    query.text.reserve(sql.size());

    enum class State
    {
        Normal,
        SingleQuoted,
        DoubleQuoted,
        LineComment,
        BlockComment
    };

    State state = State::Normal;
    for (std::size_t i = 0; i < sql.size(); ++i)
    {
        const char c = sql[i];
        const char next = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        switch (state)
        {
            case State::SingleQuoted:
                // Literal data: `?` stays a question mark and `:` is plain text,
                // exactly like the adapters' template parsers see it.
                query.text += c;
                if (c == '\'')
                {
                    if (next == '\'')
                    {
                        query.text += next;
                        ++i;
                    }
                    else
                    {
                        state = State::Normal;
                    }
                }

                break;

            case State::LineComment:
                if (c == ':')
                {
                    throwColonReserved(sql);
                }

                query.text += c;
                if (c == '\n')
                {
                    state = State::Normal;
                }

                break;

            case State::BlockComment:
                if (c == ':')
                {
                    throwColonReserved(sql);
                }

                query.text += c;
                if (c == '*' && next == '/')
                {
                    query.text += next;
                    ++i;
                    state = State::Normal;
                }

                break;

            case State::DoubleQuoted:
                // Identifiers pass through verbatim; `?` in them is literal text
                // for the adapters too, but `:` would be picked up as a template
                // parameter.
                if (c == ':')
                {
                    throwColonReserved(sql);
                }

                query.text += c;
                if (c == '"')
                {
                    if (next == '"')
                    {
                        query.text += next;
                        ++i;
                    }
                    else
                    {
                        state = State::Normal;
                    }
                }

                break;

            case State::Normal:
                if (c == '-' && next == '-')
                {
                    query.text += "--";
                    ++i;
                    state = State::LineComment;
                }
                else if (c == '/' && next == '*')
                {
                    query.text += "/*";
                    ++i;
                    state = State::BlockComment;
                }
                else if (c == '\'')
                {
                    query.text += c;
                    state = State::SingleQuoted;
                }
                else if (c == '"')
                {
                    query.text += c;
                    state = State::DoubleQuoted;
                }
                else if (c == ':')
                {
                    throwColonReserved(sql);
                }
                else if (c == '?')
                {
                    const std::string name = "p" + std::to_string(query.paramNames.size());
                    query.text += ":";
                    query.text += name;
                    query.paramNames.push_back(name);
                }
                else
                {
                    query.text += c;
                }

                break;
        }
    }

    return query;
}

std::unordered_map<oatpp::String, oatpp::Void> toOrmParams(const OrmQuery& query,
                                                           const std::vector<std::string>& params)
{
    if (query.paramNames.size() != params.size())
    {
        throw errors::database::QueryFailed("SQL expects " + std::to_string(query.paramNames.size()) +
                                            " bound parameter(s) but " + std::to_string(params.size()) +
                                            " were supplied");
    }

    std::unordered_map<oatpp::String, oatpp::Void> out;
    out.reserve(params.size());
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        out.emplace(oatpp::String(query.paramNames[i]), oatpp::Void(oatpp::String(params[i])));
    }

    return out;
}

std::string queryResultError(const std::shared_ptr<oatpp::orm::QueryResult>& result)
{
    const oatpp::String message = result->getErrorMessage();
    return message ? std::string(*message) : std::string("unknown error");
}

std::vector<std::vector<std::string>> toStdRows(oatpp::Vector<oatpp::Vector<oatpp::String>> rows)
{
    std::vector<std::vector<std::string>> out;
    out.reserve(static_cast<std::size_t>(rows->size()));

    for (auto& row : *rows)
    {
        std::vector<std::string> cells;
        cells.reserve(static_cast<std::size_t>(row->size()));

        for (auto& cell : *row)
        {
            cells.push_back(cell ? std::string(*cell) : std::string());
        }

        out.push_back(std::move(cells));
    }

    return out;
}

std::string ormErrorDetail(const std::exception& ex)
{
    const std::string message = ex.what();
    const std::string marker = "Can't connect. ";
    const std::size_t at = message.find(marker);
    if (at == std::string::npos)
    {
        return message;
    }

    return message.substr(at + marker.size());
}

}  // namespace app::infrastructure::database
