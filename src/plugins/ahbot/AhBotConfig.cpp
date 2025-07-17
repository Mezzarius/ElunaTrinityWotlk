#include "../pchdef.h"
#include "AhBotConfig.h"
#include <utility>
#include <map>
std::vector<std::string> split(const std::string &s, char delim);

using namespace std;

AhBotConfig::AhBotConfig()
{
}

template <class T>
void LoadSet(string value, T &res)
{
    vector<string> ids = split(value, ',');
    for (vector<string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        uint32 id = atoi((*i).c_str());
        if (!id)
            continue;

        res.insert(id);
    }
}
bool AhBotConfig::AddData()
{
    std::map<uint8, std::string> category;
    uint8 i = 0;
    category[i] = "equip";
    category[++i] = "reagent";
    category[++i] = "other";
    category[++i] = "quiver";
    category[++i] = "container";
    category[++i] = "glyph";
    category[++i] = "quest";
    category[++i] = "alchemy";
    category[++i] = "scroll";
    category[++i] = "Food";
    category[++i] = "bandage";
    category[++i] = "OtherConsumable";
    category[++i] = "recipe";
    category[++i] = "Elemental";
    category[++i] = "Cloth";
    category[++i] = "Leather";
    category[++i] = "Herb";
    category[++i] = "Meat";
    category[++i] = "Metal";
    category[++i] = "Engineering";
    category[++i] = "Disenchants";
    category[++i] = "SimpleGems";
    category[++i] = "SocketGems";
    category[++i] = "OtherTrade";

    // std::string type[] = { ".gray", ".white", ".green", ".blue", ".epic", "" };
    std::pair <std::string, int32> bar;
    for (uint32 realname = 0; realname < category.size(); ++realname)
    {
        for (uint32 quality = 0; quality < type.size(); ++quality)
        {
            string name = "";
            string name2 = "";
            name = "AhBot.MaxAuctionCount." + category[realname] + type[quality];
            name2 = category[realname] + type[quality];
            int num = GetIntDefault(name.c_str(), 5);
            bar = std::pair<std::string, int32>{ name2, num };
            maxAuctionNumber.insert(bar);
            // maxAuctionCount.insert[category[i].c_str() + type[quality].c_str()] = sAhBotConfig.GetMaxAllowedAuctionCount("AhBot.MaxAuctionCount." + category[i] + type[quality]));
            sLog->outMessage("ahbot", LOG_LEVEL_INFO, "----- [%s: %i]\n", category[realname].c_str(), GetIntDefault(name.c_str(), 5));
        }
    }
    return true;
}
bool AhBotConfig::Initialize()
{
    string error;
    vector<string> args;
    if (!config.LoadInitial("ahbot.conf", args, error))
    {
        sLog->outMessage("ahbot", LOG_LEVEL_INFO, "AhBot is Disabled. Unable to open configuration file ahbot.conf");
        return false;
    }

    enabled = config.GetBoolDefault("AhBot.Enabled", true);

    if (!enabled)
        sLog->outMessage("ahbot", LOG_LEVEL_INFO, "AhBot is Disabled in ahbot.conf");

    guid = (uint64)config.GetIntDefault("AhBot.GUID", 0);
    updateInterval = config.GetIntDefault("AhBot.UpdateIntervalInSeconds", 300);
	updateItemNum = config.GetIntDefault("AhBot.UpdateItemNumbers", 40);
	historyDays = config.GetIntDefault("AhBot.History.Days", 30);
    itemBuyMinInterval = config.GetIntDefault("AhBot.ItemBuyMinInterval", 600);
    itemBuyMaxInterval = config.GetIntDefault("AhBot.ItemBuyMaxInterval", 7200);
    itemSellMinInterval = config.GetIntDefault("AhBot.ItemSellMinInterval", 600);
    itemSellMaxInterval = config.GetIntDefault("AhBot.ItemSellMaxInterval", 7200);
    maxSellInterval = config.GetIntDefault("AhBot.MaxSellInterval", 3600 * 8);
    alwaysAvailableMoney = config.GetIntDefault("AhBot.AlwaysAvailableMoney", 200000);
    priceMultiplier = config.GetFloatDefault("AhBot.PriceMultiplier", 1.0f);
    defaultMinPrice = config.GetIntDefault("AhBot.DefaultMinPrice", 20);
    maxItemLevel = config.GetIntDefault("AhBot.MaxItemLevel", 199);
    maxRequiredLevel = config.GetIntDefault("AhBot.MaxRequiredLevel", 80);
    priceQualityMultiplier = config.GetFloatDefault("AhBot.PriceQualityMultiplier", 1.0f);
    underPriceProbability = config.GetFloatDefault("AhBot.UnderPriceProbability", 0.05f);
    LoadSet<set<uint32> >(config.GetStringDefault("AhBot.IgnoreItemIds", "49283,52200,8494,6345,6891,2460,37164,34835"), ignoreItemIds);
    ReloadConfigNumbers();
    return enabled;
}
