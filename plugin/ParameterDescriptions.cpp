#include "ParameterDescriptions.h"

namespace fs1rplug {

bool loadParameterDescriptions(const void* jsonData, int numBytes, std::vector<ParamDesc>& out) {
    out.clear();
    auto doc = juce::JSON::parse(juce::String::createStringFromData(jsonData, numBytes));
    auto* root = doc.getDynamicObject();
    if (root == nullptr) return false;
    auto list = root->getProperty("parameters");
    if (!list.isArray()) return false;

    for (auto& item : *list.getArray()) {
        auto* o = item.getDynamicObject();
        if (o == nullptr) continue;
        auto addr = o->getProperty("address");
        if (!addr.isArray() || addr.getArray()->size() != 3) continue;

        ParamDesc d;
        d.name = o->getProperty("name").toString();
        d.group = o->getProperty("group").toString();
        d.description = o->getProperty("description").toString();
        d.unit = o->getProperty("unit").toString();
        for (int i = 0; i < 3; ++i) d.addr[i] = (int)addr.getArray()->getUnchecked(i);
        d.min = (int)o->getProperty("min");
        d.max = (int)o->getProperty("max");
        d.def = o->hasProperty("default") ? (int)o->getProperty("default") : d.min;
        d.shift = (int)o->getProperty("shift");
        d.width = (int)o->getProperty("width");
        d.displayOffset = (int)o->getProperty("displayOffset");
        d.bipolar = (bool)o->getProperty("bipolar");
        d.discrete = (bool)o->getProperty("discrete");
        d.wide = (bool)o->getProperty("wide");
        d.partRelative = (bool)o->getProperty("partRelative");
        auto vals = o->getProperty("values");
        if (vals.isArray())
            for (auto& v : *vals.getArray()) d.values.add(v.toString());
        if (d.max < d.min) std::swap(d.min, d.max);
        d.def = juce::jlimit(d.min, d.max, d.def);
        out.push_back(std::move(d));
    }
    return !out.empty();
}

}  // namespace fs1rplug
