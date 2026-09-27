#include "util/JsonText.h"

namespace kestrel::util {

std::string stripJsonComments(const std::string& source)
{
    std::string out;
    out.reserve(source.size());
    bool inString = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (inString) {
            out.push_back(c);
            if (c == '\\' && i + 1 < source.size()) {
                out.push_back(source[++i]);
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(c);
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            out.push_back('\n');
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                ++i;
            }
            ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::unique_ptr<json::Value> parseJsonObject(const std::string& text)
{
    std::unique_ptr<json::Value> root = json::parse(stripJsonComments(text));
    if (!root || !root->isObject()) {
        return nullptr;
    }
    return root;
}

float jsonNumber(const json::Value* value, float fallback)
{
    return value && value->isNumber() ? static_cast<float>(value->mNumber) : fallback;
}

bool jsonBool(const json::Value* value, bool fallback)
{
    return value && value->mType == json::Value::Type::Boolean ? value->mBoolean : fallback;
}

void jsonRange(const json::Value* value, float& low, float& high)
{
    if (!value) {
        return;
    }
    if (value->isNumber()) {
        low = high = static_cast<float>(value->mNumber);
    } else if (value->isArray() && value->mArray.size() >= 2 && value->mArray[0]->isNumber() && value->mArray[1]->isNumber()) {
        low = static_cast<float>(value->mArray[0]->mNumber);
        high = static_cast<float>(value->mArray[1]->mNumber);
    }
}

}
