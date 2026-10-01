#pragma once

#include <string>

namespace Frigate::Level {

// HATTYPE.
enum class HatType { Other = -1, Beret, Bird, Peaked, Helmet, Furry, Moon };

// get_hat_model, by the name of the hat's model.
inline HatType HatTypeOf(const std::string& modelName) {
    if (modelName == "hatfurry" || modelName == "hatfurrybrown" || modelName == "hatfurryblack") {
        return HatType::Furry;
    }
    if (modelName == "hattbird" || modelName == "hattbirdbrown") {
        return HatType::Bird;
    }
    if (modelName == "hathelmet" || modelName == "hathelmetgrey") {
        return HatType::Helmet;
    }
    if (modelName == "hatmoon") {
        return HatType::Moon;
    }
    if (modelName == "hatberet" || modelName == "hatberetblue" || modelName == "hatberetred") {
        return HatType::Beret;
    }
    if (modelName == "hatpeaked") {
        return HatType::Peaked;
    }
    return HatType::Other;
}

} // namespace Frigate::Level
