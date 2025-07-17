#pragma once

#include "../../common/Configuration/Config.h"

using namespace std;

class AhBotConfig
{
public:
    AhBotConfig();
    static AhBotConfig& instance()
    {
        static AhBotConfig instance;
        return instance;
    }

public:

    // std::string GetType(uint8 which) { if (which > 5) which = 5; return QualityValue::type[which]; }

    bool Initialize();
    bool AddData();
    bool enabled;
    uint64 guid;
    uint32 updateInterval;
	uint16 updateItemNum;
    uint32 historyDays, maxSellInterval;
    uint32 itemBuyMinInterval, itemBuyMaxInterval;
    uint32 itemSellMinInterval, itemSellMaxInterval;
    uint32 alwaysAvailableMoney;
    float priceMultiplier, priceQualityMultiplier;
    uint32 defaultMinPrice;
    uint32 maxItemLevel, maxRequiredLevel;
    float underPriceProbability;
    std::set<uint32> ignoreItemIds;

    float GetSellPriceMultiplier(string category)
    {
        return GetCategoryParameter(sellPriceMultipliers, "PriceMultiplier.Sell", category, 1.0f);
    }

    float GetBuyPriceMultiplier(string category)
    {
        return GetCategoryParameter(buyPriceMultipliers, "PriceMultiplier.Buy", category, 1.0f);
    }

    float GetItemPriceMultiplier(string name)
    {
        return GetCategoryParameter(itemPriceMultipliers, "PriceMultiplier.Item", name, 1.0f);
    }

    int32 GetMaxAllowedAuctionCount(string category)
    {
        return (int32)GetCategoryParameter(maxAuctionCount, "MaxAuctionCount", category, 5);
    }

    std::string GetStringDefault(const char* name, const char* def)
    {
        return config.GetStringDefault(name, def);
    }

    bool GetBoolDefault(const char* name, const bool def = false)
    {
        return config.GetBoolDefault(name, def);
    }

    int32 GetIntDefault(const char* name, const int32 def)
    {
        return config.GetIntDefault(name, def);
    }

    float GetFloatDefault(const char* name, const float def)
    {
        return config.GetFloatDefault(name, def);
    }
    int32 GetMaxAuctionNumber(const char* name, const int32 def)
    {
        if (maxAuctionNumber.find(name) != maxAuctionNumber.end())
            return maxAuctionNumber[name];
        else
            return def;
    }
    std::string GetType(uint8 which)
    {
        if (which > 5)
            which = 5;
        return type[which];

    }
    bool ReloadConfigNumbers()
    {
        type[0] = ".gray";
        type[1] = ".white";
        type[2] = ".green";
        type[3] = ".blue";
        type[4] = ".epic";
        type[5] = "";

        maxAuctionNumber.clear();
        return AddData();
    }
    std::string QualityValue(uint8 which)
    {
        if (which > 5)
            which = 5;
        return type[which];
    };

private:
    float GetCategoryParameter(map<string, float>& cache, string type, string category, float defaultValue)
    {
        if (cache.find(category) == cache.end())
        {
            // ostringstream out; out << "AhBot."<< type << "." << category; // Edited by XEQT
            // ostringstream out;
            // out << "AhBot."<< type << "." << category; // Edited by XEQT
            // GetFloatDefault(out.str().c_str(), defaultValue);
            //if (cache.find(category) == cache.end())
            cache[category] = defaultValue; 
        }
        // printf("Read value => [%f]\n", cache[category]);
        return cache[category];
    }

private:
    ConfigMgr config;
    map<string, float> sellPriceMultipliers;
    map<string, float> buyPriceMultipliers;
    map<string, float> itemPriceMultipliers;
    map<string, float> maxAuctionCount;
    map<string, int32> maxAuctionNumber;
    map<uint8, string> type;

};

#define sAhBotConfig AhBotConfig::instance()
