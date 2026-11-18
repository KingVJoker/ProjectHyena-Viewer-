#include "llviewerprecompiledheaders.h"
#include "lllauncherfloater.h"
#include "llui.h"

// Viewer includes
#include "llbutton.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llcheckboxctrl.h"
#include "llstartup.h"
#include "lllogininstance.h"
#include "llwindow.h"
#include "llfontregistry.h"
#include "llweb.h"
#include "llviewercontrol.h"
#include <functional>
#include "llfloaterreg.h"
#include "llsecapi.h"
#include "llmd5.h"
#include "llviewernetwork.h"
#include "llslurl.h"
#include "lluri.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "llnotificationsutil.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llcoros.h"
#include "llcorehttputil.h"
#include "llevents.h"
#include "lltextbox.h"
#include "lliconctrl.h"
#include "lltexteditor.h"
#include "llstyle.h"
#include "llfontgl.h"
#include "lltimer.h"
#include "llimage.h"
#include "lluiimage.h"
#include "llviewertexture.h"
#include "llviewertexturelist.h"
#include "llworldmap.h"
#include "llviewerwindow.h"
#include "llrootview.h"
#include "llviewermenu.h"
#include "llmenugl.h"
#include "llstatusbar.h"
#include "llprogressview.h"
#include "llnavigationbar.h"
#include "lldockablefloater.h"
#include "lllayoutstack.h"
#include "llvoiceclient.h"
#include "llviewerparcelmgr.h"
#include "llpanelpresetspulldown.h"
#include "llpanelpresetscamerapulldown.h"
#include "llpanelvolumepulldown.h"
#include "llpanelnearbymedia.h"
#include "llagent.h"
#include "llagentui.h"
#include "llpaneltopinfobar.h"
#include "llviewerstats.h"
#include "lltrace.h"
#include "lltracerecording.h"
#include "llappviewer.h"
#if LL_WINDOWS
#include "llwindowwin32.h"
#endif
#include "llworldmapmessage.h"
#include "message.h"
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <memory>
#include <vector>

// Tracks whether the password box is currently showing dots (true) or plain text (false)
static bool sPasswordHidden = true;

// Shows or hides the password text and swaps which eye icon is visible.
static void setLauncherPasswordVisible(LLPanel* panel, LLLineEditor* edit, bool visible, bool take_focus)
{
    sPasswordHidden = !visible;
    if (edit)
    {
        edit->setDrawAsterixes(!visible);
        // Re-set the same text so the box rebuilds what it shows (dots <-> plain text)
        // right away, instead of waiting for a selection or edit to refresh it.
        const std::string current_text = edit->getText();
        edit->setText(current_text);
        if (take_focus)
        {
            edit->setFocus(true);
        }
    }
    LLButton* show_btn = panel->findChild<LLButton>("show_password_btn");
    LLButton* hide_btn = panel->findChild<LLButton>("hide_password_btn");
    if (show_btn)
    {
        show_btn->setVisible(!visible);
    }
    if (hide_btn)
    {
        hide_btn->setVisible(visible);
    }
    LL_INFOS("HyenaDebug") << "HYENA: password is now " << (visible ? "visible" : "hidden") << LL_ENDL;
}

// Makes "John.Doe", "john_doe", "John Doe" and "John Doe Resident" all compare equal.
static std::string normalize_account_name(std::string name)
{
    LLStringUtil::trim(name);
    LLStringUtil::toLower(name);
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == '.' || name[i] == '_')
        {
            name[i] = ' ';
        }
    }
    const std::string suffix = " resident";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

// What goes in the username box: the saved account name without the " Resident" the grid adds
// to single-name accounts ("MrJ Resident" -> "MrJ"). Typing just the username already logs in fine.
static std::string username_for_display(std::string name)
{
    LLStringUtil::trim(name);
    std::string lower = name;
    LLStringUtil::toLower(lower);
    // the grid stores it as "mrj_resident" (or "mrj resident" / "mrj.resident")
    static const char* SUFFIXES[] = { "_resident", " resident", ".resident" };
    for (size_t i = 0; i < sizeof(SUFFIXES) / sizeof(SUFFIXES[0]); ++i)
    {
        const std::string suffix(SUFFIXES[i]);
        if (lower.size() > suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            name.erase(name.size() - suffix.size());
            break;
        }
    }
    return name;
}

// ---------------------------------------------------------------------
//  SAVED ACCOUNTS: the username box turns into a drop-down when more than one account has been saved
// ---------------------------------------------------------------------
// Fills the drop-down from the viewer's saved logins ("login_list") and returns how many there are.
static int load_saved_accounts(LLComboBox* combo)
{
    if (!combo)
    {
        return 0;
    }
    combo->removeall();
    int               count = 0;
    const std::string grid  = LLGridManager::getInstance()->getGrid();
    if (gSecAPIHandler->hasCredentialMap("login_list", grid))
    {
        LLSecAPIHandler::credential_map_t credentials;
        gSecAPIHandler->loadCredentialMap("login_list", grid, credentials);
        for (LLSecAPIHandler::credential_map_t::iterator it = credentials.begin(); it != credentials.end(); ++it)
        {
            if (it->second.isNull())
            {
                continue;
            }
            const LLSD& ident = it->second->getIdentifier();
            std::string name  = "unknown";
            if (ident.isMap() && ident["type"].asString() == "agent")
            {
                name             = ident["first_name"].asString();
                std::string last = ident["last_name"].asString();
                LLStringUtil::toLower(last);
                if (!last.empty() && last != "resident")
                {
                    name += " " + ident["last_name"].asString();
                }
            }
            else if (ident.isMap() && ident["type"].asString() == "account")
            {
                name = ident["account_name"].asString();
            }
            combo->add(name, LLSD(it->first), ADD_BOTTOM, true);
            ++count;
        }
    }
    return count;
}

// One saved account (or none): the plain typing box. Two or more: the drop-down with its arrow.
static void show_account_controls(LLPanel* panel, int saved_count)
{
    const bool dropdown = saved_count > 1;
    LLView*    edit     = panel->findChildView("username_edit");
    LLView*    combo    = panel->findChildView("username_combo");
    LLView*    arrow    = panel->findChildView("username_arrow");
    if (edit)
    {
        edit->setVisible(!dropdown);
    }
    if (combo)
    {
        combo->setVisible(dropdown);
        combo->setEnabled(true);
    }
    if (arrow)
    {
        arrow->setVisible(dropdown);
    }
}

// Shown in the password box when a password is remembered (the real one is never put on screen).
// If the box still holds this when you launch, the saved password is used.
static const char* const PASSWORD_FILLER = "123456789!123456";

struct LauncherFavorite
{
    F64         order;
    std::string name;
    std::string slurl;
};

static bool favorite_less(const LauncherFavorite& a, const LauncherFavorite& b)
{ return a.order < b.order; }

// Reads the favorites the viewer saved for one account. The viewer keeps them in
// stored_favorites_<grid>.xml in the user settings folder:
//   { "Account Name": [ { name, slurl, asset_id }, ... ] }     (a list, in favorites-bar order)
// A map keyed by position is read as well, in case another version wrote it that way.
static std::vector<LauncherFavorite> read_saved_favorites(const std::string& account)
{
    std::vector<LauncherFavorite> favorites;
    if (account.empty())
    {
        return favorites;
    }

    // The file name the viewer uses, then any other stored_favorites*.xml that is there
    const std::string        dir = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "");
    std::vector<std::string> files;
    files.push_back(
        gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "stored_favorites_" + LLGridManager::getInstance()->getGrid() + ".xml"));
    files.push_back(
        gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "stored_favorites_" + LLGridManager::getInstance()->getGridId() + ".xml"));
    {
        LLDirIterator iter(dir, "stored_favorites*.xml");
        std::string   name;
        while (iter.next(name))
        {
            const std::string path = gDirUtilp->add(dir, name);
            if (std::find(files.begin(), files.end(), path) == files.end())
            {
                files.push_back(path);
            }
        }
    }

    const std::string wanted = normalize_account_name(account);
    LLSD              chosen;       // the favorites of the matching account
    LLSD              only_account; // used when the file holds one account that did not match by name
    std::string       only_account_name;

    for (size_t i = 0; i < files.size() && !chosen.isDefined(); ++i)
    {
        llifstream file;
        file.open(files[i].c_str());
        if (!file.is_open())
        {
            continue;
        }
        LLSD all;
        LLSDSerialize::fromXML(all, file);
        file.close();
        if (!all.isMap())
        {
            LL_INFOS("HyenaDebug") << "HYENA: favorites file is not readable: " << files[i] << LL_ENDL;
            continue;
        }

        std::string names;
        for (LLSD::map_const_iterator it = all.beginMap(); it != all.endMap(); ++it)
        {
            names += (names.empty() ? "" : ", ") + it->first;
            if (normalize_account_name(it->first) == wanted)
            {
                chosen = it->second;
            }
        }
        LL_INFOS("HyenaDebug") << "HYENA: favorites file: " << files[i] << " | accounts in it: " << names << " | looking for: " << account
            << (chosen.isDefined() ? " | FOUND" : " | not found") << LL_ENDL;
        if (!chosen.isDefined() && all.size() == 1 && !only_account.isDefined())
        {
            only_account      = all.beginMap()->second;
            only_account_name = all.beginMap()->first;
        }
    }
    if (!chosen.isDefined() && only_account.isDefined())
    {
        LL_INFOS("HyenaDebug") << "HYENA: no account matched by name, using the only one saved: " << only_account_name << LL_ENDL;
        chosen = only_account;
    }

    if (chosen.isArray())
    {
        F64 position = 0;
        for (LLSD::array_const_iterator it = chosen.beginArray(); it != chosen.endArray(); ++it, position += 1)
        {
            LauncherFavorite fav;
            fav.order = position;
            fav.name  = (*it)["name"].asString();
            fav.slurl = (*it)["slurl"].asString();
            if (!fav.name.empty() && !fav.slurl.empty()) // empty slots in the list are skipped
            {
                favorites.push_back(fav);
            }
        }
    }
    else if (chosen.isMap())
    {
        for (LLSD::map_const_iterator it = chosen.beginMap(); it != chosen.endMap(); ++it)
        {
            LauncherFavorite fav;
            fav.order = atof(it->first.c_str());
            fav.name  = it->second["name"].asString();
            fav.slurl = it->second["slurl"].asString();
            if (!fav.name.empty() && !fav.slurl.empty())
            {
                favorites.push_back(fav);
            }
        }
        std::sort(favorites.begin(), favorites.end(), favorite_less);
    }
    LL_INFOS("HyenaDebug") << "HYENA: favorites read for " << account << ": " << favorites.size()
        << (chosen.isArray() ? " (list)"
            : chosen.isMap() ? " (map)"
            : " (none)")
        << LL_ENDL;
    return favorites;
}

// Rebuilds the Start Location list: Home, Last Location, Custom Region, then a FAVORITES category
// with the favorite locations saved for this account listed under it.
static void load_start_locations(LLComboBox* combo, const std::string& account)
{
    if (!combo)
    {
        return;
    }

    const LLSD previous = combo->getValue();
    combo->clearRows();
    combo->add("Home", LLSD("home"));
    combo->add("Last Location", LLSD("last"));
    combo->add("Custom Region", LLSD("CustomRegion"));

    const std::vector<LauncherFavorite> favorites = read_saved_favorites(account);

    // The FAVORITES category is always there (a heading that can't be picked), right under Custom Region
    combo->add("FAVORITES", LLSD(), ADD_BOTTOM, false);
    if (favorites.empty())
    {
        combo->add("No favorites saved yet", LLSD(), ADD_BOTTOM, false);
    }
    for (size_t i = 0; i < favorites.size(); ++i)
    {
        combo->add(favorites[i].name, LLSD(favorites[i].slurl));
    }
    LL_INFOS("HyenaDebug") << "HYENA: start location favorites found: " << favorites.size() << LL_ENDL;

    static bool first_time = true;
    if (first_time)
    {
        first_time = false;
        combo->setValue(LLSD("home")); // the launcher always opens on Home
    }
    else if (previous.isDefined() && !previous.asString().empty())
    {
        combo->setValue(previous);
    }
    else
    {
        combo->selectFirstItem();
    }
}

// True for a token like "128" or "30.5"
static bool is_number_token(const std::string& s)
{
    if (s.empty())
    {
        return false;
    }
    bool seen_dot = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '.' && !seen_dot)
        {
            seen_dot = true;
        }
        else if (s[i] < '0' || s[i] > '9')
        {
            return false;
        }
    }
    return true;
}

// Turns what the user typed into a start SLURL. Accepted:
//   Ahern
//   Ahern/128/64/30      Ahern 128 64 30      Ahern, 128, 64, 30      Da Boom 128 128 30
//   secondlife://Ahern/128/64/30   (a full SLURL is used as it is)
static bool build_region_slurl(const std::string& input, std::string& out_slurl)
{
    std::string text = input;
    LLStringUtil::trim(text);
    if (text.empty())
    {
        return false;
    }
    // Full links in any of the usual shapes are reduced to  Region/x/y/z  and rebuilt below, so
    // names with spaces (Da%20Boom, Da Boom) and map links all end up as one clean SLURL:
    //   secondlife://Ahern/128/64/30      secondlife:///app/teleport/Ahern/128/64/30
    //   http(s)://maps.secondlife.com/secondlife/Ahern/128/64/30      http(s)://slurl.com/secondlife/Ahern/128/64/30
    {
        std::string lower = text;
        LLStringUtil::toLower(lower);
        static const char* PREFIXES[] = { "secondlife:///app/teleport/",
            "secondlife:///app/region/",
            "secondlife://",
            "https://maps.secondlife.com/secondlife/",
            "http://maps.secondlife.com/secondlife/",
            "https://slurl.com/secondlife/",
            "http://slurl.com/secondlife/" };
        for (size_t i = 0; i < sizeof(PREFIXES) / sizeof(PREFIXES[0]); ++i)
        {
            const std::string prefix(PREFIXES[i]);
            if (lower.compare(0, prefix.size(), prefix) == 0)
            {
                text = text.substr(prefix.size());
                break;
            }
        }
        const size_t query = text.find_first_of("?#");
        if (query != std::string::npos)
        {
            text = text.substr(0, query);
        }
        text = LLURI::unescape(text);
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '+')
            {
                text[i] = ' ';
            }
        }
        LLStringUtil::trim(text);
        if (text.empty())
        {
            return false;
        }
    }

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '/' || text[i] == ',')
        {
            text[i] = ' ';
        }
    }

    std::vector<std::string> tokens;
    std::string              token;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == ' ')
        {
            if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        }
        else
        {
            token += text[i];
        }
    }

    // Up to three numbers at the end are X, Y, Z. Whatever is left is the region name.
    std::vector<int> coords;
    while (!tokens.empty() && coords.size() < 3 && is_number_token(tokens.back()))
    {
        coords.insert(coords.begin(), (int)atof(tokens.back().c_str()));
        tokens.pop_back();
    }
    if (tokens.empty())
    {
        return false;
    }

    std::string region;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (i > 0)
        {
            region += "%20";
        }
        region += tokens[i];
    }

    int x = 128, y = 128, z = 0;
    if (coords.size() > 0)
        x = coords[0];
    if (coords.size() > 1)
        y = coords[1];
    if (coords.size() > 2)
        z = coords[2];

    out_slurl = "secondlife://" + region + "/" + llformat("%d", x) + "/" + llformat("%d", y) + "/" + llformat("%d", z);
    return true;
}

// What was picked before "Custom Region", so the arrow in the typing box can go back to it.
static std::string sLastListChoice = "home";

// The Start Location is ONE box. It is a drop-down normally; while "Custom Region" is picked the
// same spot turns into a typing box (with a hint that disappears as soon as you type) and an arrow
// that brings the drop-down back.
static void update_region_box(LLPanel* panel, LLComboBox* combo, bool take_focus)
{
    if (!combo)
    {
        return;
    }
    LLLineEditor* region_edit = panel->findChild<LLLineEditor>("region_edit");
    LLButton*     arrow       = panel->findChild<LLButton>("region_arrow_btn");
    LLTextBox*    hint        = panel->findChild<LLTextBox>("region_placeholder");

    const std::string value  = combo->getValue().asString();
    const bool        custom = (value == "CustomRegion");
    if (!custom && !value.empty())
    {
        sLastListChoice = value;
    }

    combo->setVisible(!custom);
    LLIconCtrl* combo_arrow = panel->findChild<LLIconCtrl>("start_location_arrow");
    if (combo_arrow)
    {
        combo_arrow->setVisible(!custom);
    }
    if (region_edit)
    {
        region_edit->setVisible(custom);
        if (custom && take_focus)
        {
            region_edit->setFocus(true);
        }
    }
    if (arrow)
    {
        arrow->setVisible(custom);
    }
    if (hint)
    {
        hint->setVisible(custom && region_edit && region_edit->getText().empty());
    }
}

// Keeps the hint in the typing box in step with what is typed (checked every frame).
static void sync_region_hint(LLPanel* panel)
{
    LLLineEditor* region_edit = panel->findChild<LLLineEditor>("region_edit");
    LLTextBox*    hint        = panel->findChild<LLTextBox>("region_placeholder");
    if (region_edit && hint)
    {
        hint->setVisible(region_edit->getVisible() && region_edit->getText().empty());
    }
}

// Tells the login which start location was picked.
static void apply_start_location(LLPanel* panel, LLComboBox* combo)
{
    if (!combo)
    {
        return;
    }
    const std::string value = combo->getValue().asString();
    if (value == "home")
    {
        LLStartUp::setStartSLURL(LLSLURL(LLSLURL::SIM_LOCATION_HOME));
    }
    else if (value == "last")
    {
        LLStartUp::setStartSLURL(LLSLURL(LLSLURL::SIM_LOCATION_LAST));
    }
    else if (value == "CustomRegion")
    {
        LLLineEditor* region_edit = panel->findChild<LLLineEditor>("region_edit");
        std::string   slurl_text;
        if (region_edit && build_region_slurl(region_edit->getText(), slurl_text))
        {
            LLSLURL slurl(slurl_text);
            if (slurl.isValid())
            {
                LLStartUp::setStartSLURL(slurl);
            }
            LL_INFOS("HyenaDebug") << "HYENA: custom region: " << slurl_text << (slurl.isValid() ? "" : " (not valid)") << LL_ENDL;
        }
        else
        {
            LL_INFOS("HyenaDebug") << "HYENA: custom region box is empty, start location left as it was" << LL_ENDL;
        }
        return;
    }
    else if (!value.empty())
    {
        LLSLURL slurl(value);
        if (slurl.isValid())
        {
            LLStartUp::setStartSLURL(slurl);
        }
    }
    LL_INFOS("HyenaDebug") << "HYENA: start location chosen: " << value << LL_ENDL;
}

// =====================================================================
//  TOP HALF: live clocks, grid status and news
// =====================================================================

static const LLColor4 COLOR_OK(0.20f, 0.90f, 0.50f, 1.0f);
static const LLColor4 COLOR_WARN(1.00f, 0.80f, 0.20f, 1.0f);
static const LLColor4 COLOR_BAD(1.00f, 0.35f, 0.35f, 1.0f);
static const LLColor4 COLOR_INFO(0.40f, 0.70f, 1.00f, 1.0f);
static const LLColor4 COLOR_CYAN(0.00f, 0.90f, 1.00f, 1.0f);
static const LLColor4 COLOR_TEXT(0.90f, 0.94f, 0.97f, 1.0f);
static const LLColor4 COLOR_MUTED(0.62f, 0.70f, 0.78f, 1.0f);
static const LLColor4 COLOR_PURPLE(0.72f, 0.55f, 1.00f, 1.0f);
static const LLColor4 COLOR_TEXT_WHITE(1.00f, 1.00f, 1.00f, 1.0f);

static void set_label(LLPanel* panel, const std::string& name, const std::string& text, const LLColor4* color = NULL)
{
    LLTextBox* box = panel->findChild<LLTextBox>(name);
    if (box)
    {
        box->setText(text);
        if (color)
        {
            box->setColor(*color);
        }
    }
}

// ---- clocks ---------------------------------------------------------

static void to_utc(time_t t, std::tm& out)
{
#if LL_WINDOWS
    gmtime_s(&out, &t);
#else
    gmtime_r(&t, &out);
#endif
}

static void to_local(time_t t, std::tm& out)
{
#if LL_WINDOWS
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
}

// 0 = Sunday
static int day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
    {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Days since 1970-01-01 for a calendar date.
static long long days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int first_sunday(int year, int month)
{ return 1 + ((7 - day_of_week(year, month, 1)) % 7); }

enum ClockRule
{
    RULE_NONE,
    RULE_US, // 2nd Sunday of March to 1st Sunday of November
    RULE_EU, // last Sunday of March to last Sunday of October
    RULE_AU  // 1st Sunday of October to 1st Sunday of April (southern hemisphere)
};

struct ClockZone
{
    const char* id;
    int         std_minutes; // offset from UTC in standard time
    ClockRule   rule;
    const char* std_name;
    const char* dst_name;
};

static const ClockZone CLOCK_ZONES[] = {
    { "utc", 0, RULE_NONE, "UTC", "UTC" },
    { "us_eastern", -300, RULE_US, "EASTERN STANDARD TIME", "EASTERN DAYLIGHT TIME" },
    { "us_central", -360, RULE_US, "CENTRAL STANDARD TIME", "CENTRAL DAYLIGHT TIME" },
    { "us_mountain", -420, RULE_US, "MOUNTAIN STANDARD TIME", "MOUNTAIN DAYLIGHT TIME" },
    { "us_pacific", -480, RULE_US, "PACIFIC STANDARD TIME", "PACIFIC DAYLIGHT TIME" },
    { "us_hawaii", -600, RULE_NONE, "HAWAII STANDARD TIME", "HAWAII STANDARD TIME" },
    { "uk", 0, RULE_EU, "GREENWICH MEAN TIME", "BRITISH SUMMER TIME" },
    { "cet", 60, RULE_EU, "CENTRAL EUROPEAN TIME", "CENTRAL EUROPEAN SUMMER TIME" },
    { "eet", 120, RULE_EU, "EASTERN EUROPEAN TIME", "EASTERN EUROPEAN SUMMER TIME" },
    { "india", 330, RULE_NONE, "INDIA STANDARD TIME", "INDIA STANDARD TIME" },
    { "japan", 540, RULE_NONE, "JAPAN STANDARD TIME", "JAPAN STANDARD TIME" },
    { "au_sydney", 600, RULE_AU, "AUSTRALIAN EASTERN STANDARD TIME", "AUSTRALIAN EASTERN DAYLIGHT TIME" },
};

static const ClockZone* find_zone(const std::string& id)
{
    for (size_t i = 0; i < sizeof(CLOCK_ZONES) / sizeof(CLOCK_ZONES[0]); ++i)
    {
        if (id == CLOCK_ZONES[i].id)
        {
            return &CLOCK_ZONES[i];
        }
    }
    return NULL;
}

// Is daylight saving time in effect in this zone at this moment?
static bool zone_is_dst(const ClockZone& z, time_t now, const std::tm& utc)
{
    const int       year    = utc.tm_year + 1900;
    const long long t       = (long long)now;
    const long long std_sec = z.std_minutes * 60LL;

    switch (z.rule)
    {
    case RULE_US:
    {
        const long long start = days_from_civil(year, 3, first_sunday(year, 3) + 7) * 86400LL + 2 * 3600LL - std_sec;
        const long long end   = days_from_civil(year, 11, first_sunday(year, 11)) * 86400LL + 2 * 3600LL - (std_sec + 3600LL);
        return t >= start && t < end;
    }
    case RULE_EU:
    {
        const long long start = days_from_civil(year, 3, 31 - day_of_week(year, 3, 31)) * 86400LL + 3600LL;
        const long long end   = days_from_civil(year, 10, 31 - day_of_week(year, 10, 31)) * 86400LL + 3600LL;
        return t >= start && t < end;
    }
    case RULE_AU:
    {
        const long long start = days_from_civil(year, 10, first_sunday(year, 10)) * 86400LL + 2 * 3600LL - std_sec;
        const long long end   = days_from_civil(year, 4, first_sunday(year, 4)) * 86400LL + 2 * 3600LL - std_sec;
        return t >= start || t < end;
    }
    default:
        return false;
    }
}

// Writes "hh:mm:ss AM" for a zone and tells whether it is on daylight saving time.
static std::string zone_time_text(const ClockZone& z, time_t now, const std::tm& utc, bool& dst)
{
    dst = zone_is_dst(z, now, utc);
    std::tm zt;
    to_utc(now + (z.std_minutes + (dst ? 60 : 0)) * 60, zt);
    char buf[64];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", &zt);
    return buf;
}

// "UTC-4", "UTC+5:30", "UTC+0"
static std::string utc_offset_text(int minutes)
{
    const char sign = (minutes < 0) ? '-' : '+';
    const int  m    = (minutes < 0) ? -minutes : minutes;
    if (m % 60 == 0)
    {
        return llformat("UTC%c%d", sign, m / 60);
    }
    return llformat("UTC%c%d:%02d", sign, m / 60, m % 60);
}

// Same order as CLOCK_ZONES
static const char* const CLOCK_ZONE_MENU_NAMES[] = {
    "UTC",    "Eastern Time (US)",  "Central Time (US)", "Mountain Time (US)", "Pacific Time (US)",
    "Hawaii", "London (UK)",        "Central Europe",    "Eastern Europe",     "India",
    "Japan",  "Sydney (Australia)",
};

// Short names (standard, daylight), same order as CLOCK_ZONES
static const char* const CLOCK_ZONE_ABBR[][2] = {
    { "UTC", "UTC" }, { "EST", "EDT" },  { "CST", "CDT" },  { "MST", "MDT" }, { "PST", "PDT" }, { "HST", "HST" },
    { "GMT", "BST" }, { "CET", "CEST" }, { "EET", "EEST" }, { "IST", "IST" }, { "JST", "JST" }, { "AEST", "AEDT" },
};

// This PC's offset from UTC at the moment t, in minutes (east of UTC is positive)
static int local_offset_minutes(time_t t)
{
    std::tm utc;
    std::tm loc;
    to_utc(t, utc);
    to_local(t, loc);
    std::tm gm  = utc;
    gm.tm_isdst = loc.tm_isdst;
    return (int)((long)(t - mktime(&gm)) / 60);
}

static int zone_offset_minutes(const ClockZone& z, time_t t)
{
    std::tm utc;
    to_utc(t, utc);
    return z.std_minutes + (zone_is_dst(z, t, utc) ? 60 : 0);
}

// Which entry of CLOCK_ZONES is this PC's own time zone? A zone matches when it has the same offset
// now AND six months from now (so the same daylight saving rules). -1 if none does.
static int find_local_zone_index()
{
    const time_t now    = time(NULL);
    const time_t later  = now + 182 * 86400;
    const int    local1 = local_offset_minutes(now);
    const int    local2 = local_offset_minutes(later);
    const size_t count  = sizeof(CLOCK_ZONES) / sizeof(CLOCK_ZONES[0]);
    for (size_t i = 0; i < count; ++i)
    {
        if (zone_offset_minutes(CLOCK_ZONES[i], now) == local1 && zone_offset_minutes(CLOCK_ZONES[i], later) == local2)
        {
            return (int)i;
        }
    }
    return -1;
}

// "Eastern Daylight Time" -> "EDT"; names that are already short are kept
static std::string short_zone_name(const std::string& long_name)
{
    std::string name = long_name;
    LLStringUtil::trim(name);
    if (name.find(' ') == std::string::npos)
    {
        LLStringUtil::toUpper(name);
        return name;
    }
    std::string out;
    bool        new_word = true;
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == ' ')
        {
            new_word = true;
        }
        else
        {
            if (new_word && ((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= 'a' && name[i] <= 'z')))
            {
                out += (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]);
            }
            new_word = false;
        }
    }
    return out;
}

// "UTC-4 Local Time (EDT)": this PC's own time zone, with its short name in brackets.
static std::string local_zone_label()
{
    const time_t now = time(NULL);
    std::tm      loc;
    to_local(now, loc);

    std::string abbr;
    const int   idx = find_local_zone_index();
    if (idx >= 0)
    {
        abbr = CLOCK_ZONE_ABBR[idx][zone_offset_minutes(CLOCK_ZONES[idx], now) == CLOCK_ZONES[idx].std_minutes ? 0 : 1];
    }
    else
    {
        char zone_name[128];
        strftime(zone_name, sizeof(zone_name), "%Z", &loc);
        abbr = short_zone_name(zone_name);
    }
    return utc_offset_text(local_offset_minutes(now)) + " Local Time" + (abbr.empty() ? std::string() : " (" + abbr + ")");
}

// Which zone the second clock shows ("local" = this PC's own time zone)
static std::string sClockZoneId = "local";

// Fills the time zone drop-down. "Local Time" is this PC's own zone, so the zone that matches it
// (for example Eastern Time for someone in Georgia) is not listed a second time.
static void build_zone_menu(LLComboBox* combo, const std::string& selected)
{
    if (!combo)
    {
        return;
    }
    const time_t now        = time(NULL);
    const int    local_zone = find_local_zone_index();

    combo->clearRows();
    combo->add(local_zone_label(), LLSD("local"));
    const size_t count = sizeof(CLOCK_ZONES) / sizeof(CLOCK_ZONES[0]);
    for (size_t i = 0; i < count; ++i)
    {
        if ((int)i == local_zone)
        {
            continue; // already shown as "Local Time"
        }
        if (std::string(CLOCK_ZONES[i].id) == "utc")
        {
            continue; // not offered in the list
        }
        const int         minutes = zone_offset_minutes(CLOCK_ZONES[i], now);
        const std::string label   = (CLOCK_ZONES[i].std_minutes == 0 && CLOCK_ZONES[i].rule == RULE_NONE)
            ? std::string(CLOCK_ZONE_MENU_NAMES[i])
            : utc_offset_text(minutes) + " " + CLOCK_ZONE_MENU_NAMES[i];
        combo->add(label, LLSD(CLOCK_ZONES[i].id));
    }
    combo->setValue(LLSD(selected));
}

static void update_clocks(LLPanel* panel)
{
    const time_t now = time(NULL);
    std::tm      utc;
    to_utc(now, utc);

    // SLT is always US Pacific time
    const ClockZone* pacific = find_zone("us_pacific");
    bool             slt_dst = false;
    set_label(panel, "slt_time_text", zone_time_text(*pacific, now, utc, slt_dst));
    set_label(panel, "slt_zone_text", "SLT PACIFIC TIME");

    // The second clock follows the drop-down (default: this PC's own time zone)
    const ClockZone* zone = find_zone(sClockZoneId);
    if (zone)
    {
        bool dst = false;
        set_label(panel, "local_time_text", zone_time_text(*zone, now, utc, dst));
        set_label(panel, "local_zone_text", dst ? zone->dst_name : zone->std_name);
    }
    else
    {
        std::tm loc;
        to_local(now, loc);
        char buf[64];
        strftime(buf, sizeof(buf), "%I:%M:%S %p", &loc);
        set_label(panel, "local_time_text", buf);

        char zone_name[128];
        strftime(zone_name, sizeof(zone_name), "%Z", &loc);
        std::string zone_text(zone_name);
        LLStringUtil::toUpper(zone_text);
        set_label(panel, "local_zone_text", zone_text.empty() ? std::string("LOCAL TIME") : zone_text);
    }
}

// ---- small text helpers for the feeds -------------------------------

// Text between <tag> and </tag>, "" if missing.
static std::string xml_tag(const std::string& s, const std::string& tag)
{
    const std::string open  = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    const size_t      a     = s.find(open);
    if (a == std::string::npos)
    {
        return std::string();
    }
    const size_t from = a + open.size();
    const size_t b    = s.find(close, from);
    if (b == std::string::npos)
    {
        return std::string();
    }
    return s.substr(from, b - from);
}

static void replace_all(std::string& s, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Turns &lt; &amp; &apos; ... back into the characters they stand for.
static std::string decode_entities(std::string s)
{
    replace_all(s, "&nbsp;", " ");
    replace_all(s, "&lt;", "<");
    replace_all(s, "&gt;", ">");
    replace_all(s, "&quot;", "\"");
    replace_all(s, "&apos;", "'");
    replace_all(s, "&#39;", "'");
    replace_all(s, "&#x27;", "'");
    replace_all(s, "&#8217;", "'");
    replace_all(s, "&#8211;", "-");
    replace_all(s, "&amp;", "&"); // last, so "&amp;lt;" does not turn into "<" too early
    return s;
}

// Cleans feed text. The status feed sends its html ESCAPED (&lt;p&gt;...), so the entities are
// decoded first (which turns the escaped markup into real tags), the tags are then removed,
// and the entities that were inside the text are decoded again.
static std::string strip_html(std::string s)
{
    replace_all(s, "<![CDATA[", "");
    replace_all(s, "]]>", "");
    s = decode_entities(s);

    replace_all(s, "<br>", " ");
    replace_all(s, "<br/>", " ");
    replace_all(s, "<br />", " ");
    replace_all(s, "</p>", " ");
    replace_all(s, "</small>", " ");
    replace_all(s, "</strong>", " ");

    std::string out;
    bool        in_tag = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '<')
        {
            in_tag = true;
        }
        else if (s[i] == '>')
        {
            in_tag = false;
        }
        else if (!in_tag)
        {
            out += s[i];
        }
    }
    out = decode_entities(out);

    std::string squeezed;
    bool        last_space = true;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const bool is_space = (out[i] == ' ' || out[i] == '\n' || out[i] == '\r' || out[i] == '\t');
        if (is_space)
        {
            if (!last_space)
            {
                squeezed += ' ';
            }
            last_space = true;
        }
        else
        {
            squeezed += out[i];
            last_space = false;
        }
    }
    while (!squeezed.empty() && squeezed[squeezed.size() - 1] == ' ')
    {
        squeezed.erase(squeezed.size() - 1);
    }
    return squeezed;
}

// Reads "hh:mm" at s[p]; returns the position after it, or npos if it is not a time.
static size_t read_clock_time(const std::string& s, size_t p)
{
    size_t digits = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
    {
        ++p;
        ++digits;
    }
    if (digits < 1 || digits > 2 || p >= s.size() || s[p] != ':')
    {
        return std::string::npos;
    }
    ++p;
    digits = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
    {
        ++p;
        ++digits;
    }
    return (digits == 2) ? p : std::string::npos;
}

// Length of a date/time stamp that starts at s[i], or 0 if there is none. Two shapes:
//   "Oct 21, 14:09 PDT"            (an update stamp)
//   "Oct 28, 07:00 - 12:00 PDT"    (a scheduled event window)
static size_t date_stamp_length(const std::string& s, size_t i)
{
    static const char* const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (i > 0 && s[i - 1] != ' ')
    {
        return 0;
    }
    bool month_ok = false;
    for (size_t k = 0; k < 12; ++k)
    {
        if (s.compare(i, 3, MONTHS[k]) == 0)
        {
            month_ok = true;
        }
    }
    if (!month_ok)
    {
        return 0;
    }
    size_t p = i + 3;
    if (p >= s.size() || s[p] != ' ')
    {
        return 0;
    }
    ++p;
    size_t digits = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
    {
        ++p;
        ++digits;
    }
    if (digits < 1 || digits > 2 || s.compare(p, 2, ", ") != 0)
    {
        return 0;
    }
    p += 2;
    p = read_clock_time(s, p);
    if (p == std::string::npos)
    {
        return 0;
    }
    if (s.compare(p, 3, " - ") == 0) // a time range: "07:00 - 12:00"
    {
        const size_t after_range = read_clock_time(s, p + 3);
        if (after_range != std::string::npos)
        {
            p = after_range;
        }
    }
    if (p >= s.size() || s[p] != ' ')
    {
        return 0;
    }
    ++p;
    size_t letters = 0;
    while (p < s.size() && s[p] >= 'A' && s[p] <= 'Z')
    {
        ++p;
        ++letters;
    }
    if (letters < 3 || letters > 4)
    {
        return 0;
    }
    if (p < s.size() && s[p] == ' ')
    {
        ++p;
    }
    return p - i;
}

// Keeps the FIRST date/time stamp in the text and removes every later one, so a date and time
// appear only once in each entry.
static std::string keep_first_date_stamp(const std::string& s)
{
    std::string out;
    bool        kept_one = false;
    size_t      i        = 0;
    while (i < s.size())
    {
        const size_t len = date_stamp_length(s, i);
        if (len > 0)
        {
            if (!kept_one)
            {
                out += s.substr(i, len);
                kept_one = true;
            }
            i += len;
        }
        else
        {
            out += s[i];
            ++i;
        }
    }
    while (!out.empty() && out[0] == ' ')
    {
        out.erase(0, 1);
    }
    while (!out.empty() && out[out.size() - 1] == ' ')
    {
        out.erase(out.size() - 1);
    }
    return out;
}

// ---- grid status ----------------------------------------------------

static void describe_component_status(const std::string& status, std::string& text, LLColor4& color)
{
    if (status == "operational")
    {
        text  = "Operational";
        color = COLOR_OK;
    }
    else if (status == "degraded_performance")
    {
        text  = "Degraded";
        color = COLOR_WARN;
    }
    else if (status == "partial_outage")
    {
        text  = "Partial Outage";
        color = COLOR_WARN;
    }
    else if (status == "major_outage")
    {
        text  = "Major Outage";
        color = COLOR_BAD;
    }
    else if (status == "under_maintenance")
    {
        text  = "Maintenance";
        color = COLOR_INFO;
    }
    else
    {
        text  = "Unknown";
        color = COLOR_MUTED;
    }
}

static int status_severity(const std::string& s)
{
    if (s == "major_outage")
    {
        return 4;
    }
    if (s == "partial_outage")
    {
        return 3;
    }
    if (s == "degraded_performance")
    {
        return 2;
    }
    if (s == "under_maintenance")
    {
        return 1;
    }
    return 0;
}

static std::string severity_status(int severity)
{
    switch (severity)
    {
    case 4:
        return "major_outage";
    case 3:
        return "partial_outage";
    case 2:
        return "degraded_performance";
    case 1:
        return "under_maintenance";
    default:
        return "operational";
    }
}

struct StatusGroup
{
    std::string id;
    std::string name;
    int         severity;
    bool        used;
};

static bool name_has(const std::string& lower_name, const char* word)
{ return lower_name.find(word) != std::string::npos; }

static void show_grid_status(LLPanel* panel, const LLSD& summary)
{
    // ---- the badge at the top right
    const LLSD        st        = summary["status"];
    std::string       desc      = st["description"].asString();
    const std::string indicator = st["indicator"].asString();
    LLStringUtil::toUpper(desc);
    if (indicator == "none")
    {
        desc = "OPERATIONAL";
    }
    else if (indicator == "minor")
    {
        desc = "MINOR ISSUES";
    }
    else if (indicator == "major")
    {
        desc = "MAJOR OUTAGE";
    }
    else if (indicator == "critical")
    {
        desc = "CRITICAL OUTAGE";
    }
    else if (indicator == "maintenance")
    {
        desc = "MAINTENANCE";
    }
    const LLColor4 overall_color = (indicator == "none")          ? COLOR_OK
        : (indicator == "minor")       ? COLOR_WARN
        : (indicator == "maintenance") ? COLOR_INFO
        : COLOR_BAD;
    set_label(panel, "grid_overall_text", desc.empty() ? std::string("STATUS UNKNOWN") : desc, &overall_color);
    LLIconCtrl* pill = panel->findChild<LLIconCtrl>("status_pill_icon");
    if (pill)
    {
        pill->setColor(overall_color);
    }
    LLIconCtrl* dot = panel->findChild<LLIconCtrl>("grid_dot_icon");
    if (dot)
    {
        dot->setColor(overall_color);
    }

    // ---- one entry per component group, with the worst status of its members
    std::vector<StatusGroup> groups;
    const LLSD&              comps = summary["components"];
    if (comps.isArray())
    {
        for (LLSD::array_const_iterator it = comps.beginArray(); it != comps.endArray(); ++it)
        {
            if ((*it)["group"].asBoolean())
            {
                StatusGroup g;
                g.id       = (*it)["id"].asString();
                g.name     = (*it)["name"].asString();
                g.severity = 0;
                g.used     = false;
                groups.push_back(g);
            }
        }
        for (LLSD::array_const_iterator it = comps.beginArray(); it != comps.endArray(); ++it)
        {
            if ((*it)["group"].asBoolean())
            {
                continue;
            }
            const int         severity = status_severity((*it)["status"].asString());
            const std::string group_id = (*it)["group_id"].asString();
            bool              placed   = false;
            if (!group_id.empty())
            {
                for (size_t i = 0; i < groups.size(); ++i)
                {
                    if (groups[i].id == group_id)
                    {
                        groups[i].severity = std::max(groups[i].severity, severity);
                        placed             = true;
                        break;
                    }
                }
            }
            if (!placed) // a component that is not in any group gets its own tile
            {
                StatusGroup g;
                g.name     = (*it)["name"].asString();
                g.severity = severity;
                g.used     = false;
                groups.push_back(g);
            }
        }
    }

    std::string all_names;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        all_names += (i ? " | " : "") + groups[i].name;
    }
    LL_INFOS("HyenaDebug") << "HYENA: status groups: " << all_names << LL_ENDL;

    // ---- four tiles: SL REGIONS, WEBSITES, SL SERVICES, INWORLD SERVICES (matched by name)
    static const char* const TILE_LABELS[4] = { "SL REGIONS", "WEBSITES", "SL SERVICES", "INWORLD SERVICES" };
    int                      assigned[4]    = { -1, -1, -1, -1 };
    std::string              labels[4]      = { TILE_LABELS[0], TILE_LABELS[1], TILE_LABELS[2], TILE_LABELS[3] };

    for (size_t i = 0; i < groups.size(); ++i)
    {
        std::string lower = groups[i].name;
        LLStringUtil::toLower(lower);
        const bool inworld = name_has(lower, "inworld") || name_has(lower, "in-world") || name_has(lower, "in world");
        int        slot    = -1;
        if (name_has(lower, "region"))
        {
            slot = 0;
        }
        else if (name_has(lower, "web"))
        {
            slot = 1;
        }
        else if (inworld)
        {
            slot = 3;
        }
        else if (name_has(lower, "service"))
        {
            slot = 2;
        }
        if (slot >= 0 && assigned[slot] < 0)
        {
            assigned[slot] = (int)i;
            groups[i].used = true;
        }
    }
    // Empty tiles take the remaining groups, worst first, under their own names
    for (int slot = 0; slot < 4; ++slot)
    {
        if (assigned[slot] >= 0)
        {
            continue;
        }
        int best = -1;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (!groups[i].used && (best < 0 || groups[i].severity > groups[best].severity))
            {
                best = (int)i;
            }
        }
        if (best >= 0)
        {
            assigned[slot]    = best;
            groups[best].used = true;
            labels[slot]      = groups[best].name;
            LLStringUtil::toUpper(labels[slot]);
        }
    }

    for (int slot = 0; slot < 4; ++slot)
    {
        const std::string label_box  = llformat("tile%d_label", slot + 1);
        const std::string status_box = llformat("tile%d_status", slot + 1);
        set_label(panel, label_box, labels[slot], &COLOR_MUTED);
        if (assigned[slot] >= 0)
        {
            std::string text;
            LLColor4    color;
            describe_component_status(severity_status(groups[assigned[slot]].severity), text, color);
            set_label(panel, status_box, text, &color);
        }
        else
        {
            set_label(panel, status_box, "N/A", &COLOR_MUTED);
        }
    }
}

// ---- news -----------------------------------------------------------

struct LauncherNewsItem
{
    std::string tag;
    std::string title;
    std::string date;
    std::string summary;
};

static void show_news(LLPanel* panel, const std::vector<LauncherNewsItem>& items)
{
    const int SLOTS = 5;
    for (int i = 1; i <= SLOTS; ++i)
    {
        const bool        has     = (i <= (int)items.size());
        const std::string base    = llformat("news%d_", i);
        const char* const parts[] = { "tag", "date", "title", "summary", "divider" };

        if (has)
        {
            const LauncherNewsItem& item      = items[i - 1];
            const LLColor4&         tag_color = (item.tag == "[HYENA RELEASE]") ? COLOR_PURPLE : COLOR_CYAN;
            set_label(panel, base + "tag", item.tag, &tag_color);
            set_label(panel, base + "date", item.date, &COLOR_MUTED);
            set_label(panel, base + "title", item.title, &COLOR_TEXT_WHITE);
            set_label(panel, base + "summary", item.summary, &COLOR_MUTED);
        }
        for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); ++p)
        {
            LLView* v = panel->findChildView(base + parts[p]);
            if (v)
            {
                v->setVisible(has);
            }
        }
    }
}

// Reads the first few <item> blocks of the Statuspage history feed.
static void parse_status_rss(const std::string& body, std::vector<LauncherNewsItem>& items)
{
    size_t pos   = 0;
    int    count = 0;
    while (count < 4)
    {
        const size_t a = body.find("<item>", pos);
        if (a == std::string::npos)
        {
            break;
        }
        const size_t b = body.find("</item>", a);
        if (b == std::string::npos)
        {
            break;
        }
        const std::string block = body.substr(a, b - a);
        pos                     = b + 7;

        LauncherNewsItem item;
        item.tag     = "[LL GRID STATUS]";
        item.title   = strip_html(xml_tag(block, "title"));
        item.summary = keep_first_date_stamp(strip_html(xml_tag(block, "description")));
        if (item.summary.size() > 160)
        {
            item.summary = item.summary.substr(0, 160) + "...";
        }
        const std::string pub   = xml_tag(block, "pubDate"); // "Tue, 07 Oct 2026 07:00:00 +0000"
        const size_t      comma = pub.find(", ");
        if (comma != std::string::npos && pub.size() >= comma + 8)
        {
            const std::string day_month = pub.substr(comma + 2, 6);                              // "07 Oct"
            item.date                   = day_month.substr(3, 3) + " " + day_month.substr(0, 2); // "Oct 07"
        }
        if (!item.title.empty())
        {
            items.push_back(item);
            ++count;
        }
    }
}

// ---- fetching -------------------------------------------------------

static bool sGridFetchRunning = false;

// Downloads a page and returns its text. False if the request failed.
static bool fetch_text(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter,
    LLCore::HttpRequest::ptr_t                  request,
    LLCore::HttpOptions::ptr_t                  options,
    const std::string&                          url,
    std::string&                                out_text)
{
    LLSD               result  = adapter->getRawAndSuspend(request, url, options);
    LLSD               results = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status  = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(results);
    if (!status)
    {
        LL_WARNS("HyenaDebug") << "HYENA: request failed: " << url << " (" << status.toString() << ")" << LL_ENDL;
        return false;
    }
    const LLSD::Binary& raw = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    out_text.assign(raw.begin(), raw.end());
    return true;
}

static void grid_status_coro(LLHandle<LLView> handle)
{
    struct Guard
    {
        ~Guard() { sGridFetchRunning = false; }
    } guard;

    LLCore::HttpRequest::policy_t               policy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("HyenaGridStatus", policy);
    LLCore::HttpRequest::ptr_t                  request = std::make_shared<LLCore::HttpRequest>();
    LLCore::HttpOptions::ptr_t                  options = std::make_shared<LLCore::HttpOptions>();
    options->setTimeout(20);

    // 1. Component status (Atlassian Statuspage JSON)
    {
        LLSD               summary = adapter->getJsonAndSuspend(request, "https://status.secondlifegrid.net/api/v2/summary.json", options);
        LLCore::HttpStatus status =
            LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(summary[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
        LLPanel* panel = dynamic_cast<LLPanel*>(handle.get());
        if (!panel)
        {
            return;
        }
        if (status)
        {
            show_grid_status(panel, summary);
        }
        else
        {
            LL_WARNS("HyenaDebug") << "HYENA: grid status request failed (" << status.toString() << ")" << LL_ENDL;
            set_label(panel, "grid_overall_text", "STATUS UNAVAILABLE", &COLOR_MUTED);
        }
    }

    // 2. Residents online
    {
        std::string body;
        const bool  ok    = fetch_text(adapter, request, options, "https://api.secondlife.com/datafeeds/secondlife.xml", body);
        LLPanel*    panel = dynamic_cast<LLPanel*>(handle.get());
        if (!panel)
        {
            return;
        }
        if (ok)
        {
            const std::string inworld = strip_html(xml_tag(body, "inworld"));
            std::string       state   = strip_html(xml_tag(body, "status"));
            LLStringUtil::toUpper(state);
            if (!inworld.empty())
            {
                const std::string text = "Online Users: " + inworld + (state == "ONLINE" ? " (Grid Active)" : " (Grid " + state + ")");
                set_label(panel, "grid_online_text", text, state == "ONLINE" ? &COLOR_OK : &COLOR_WARN);
            }
        }
    }

    // 3. News: grid status history feed
    {
        std::string body;
        const bool  ok    = fetch_text(adapter, request, options, "https://status.secondlifegrid.net/history.rss", body);
        LLPanel*    panel = dynamic_cast<LLPanel*>(handle.get());
        if (!panel)
        {
            return;
        }

        std::vector<LauncherNewsItem> items;
        LauncherNewsItem              hyena;
        hyena.tag     = "[HYENA RELEASE]";
        hyena.date    = "Today";
        hyena.title   = "v0.1.0 Alpha Build Released";
        hyena.summary = "Welcome to Project Hyena! Featuring optimized region caching, live SLT clock synchronization, and high-DPI PBR "
            "graphics presets.";
        items.push_back(hyena);
        if (ok)
        {
            parse_status_rss(body, items);
        }
        if (items.size() > 5)
        {
            items.resize(5);
        }
        show_news(panel, items);
        LL_INFOS("HyenaDebug") << "HYENA: news items shown: " << items.size() << LL_ENDL;
    }
}

static void start_grid_fetch(LLPanel* panel)
{
    if (sGridFetchRunning)
    {
        return;
    }
    sGridFetchRunning = true;
    LLCoros::instance().launch("HyenaGridStatus", boost::bind(&grid_status_coro, panel->getHandle()));
}

// =====================================================================
//  PRE-FLIGHT MAINTENANCE buttons
// =====================================================================

// A button shows a short result ("Done", "84 ms") for a few seconds, then gets its own text back.
struct ButtonFeedback
{
    std::string name;
    std::string original;
    F64         until;
};
static std::vector<ButtonFeedback> sButtonFeedback;

static void set_button_feedback(LLPanel* panel, const std::string& name, const std::string& text, F64 seconds = 3.5)
{
    LLButton* button = panel->findChild<LLButton>(name);
    if (!button)
    {
        return;
    }
    const F64 until = LLTimer::getTotalSeconds() + seconds;
    for (size_t i = 0; i < sButtonFeedback.size(); ++i)
    {
        if (sButtonFeedback[i].name == name) // already showing a result: just replace it
        {
            sButtonFeedback[i].until = until;
            button->setLabel(text);
            return;
        }
    }
    ButtonFeedback entry;
    entry.name     = name;
    entry.original = button->getLabelUnselected();
    entry.until    = until;
    sButtonFeedback.push_back(entry);
    button->setLabel(text);
}

static void update_button_feedback(LLPanel* panel)
{
    if (sButtonFeedback.empty())
    {
        return;
    }
    const F64 now = LLTimer::getTotalSeconds();
    for (size_t i = 0; i < sButtonFeedback.size();)
    {
        if (now >= sButtonFeedback[i].until)
        {
            LLButton* button = panel->findChild<LLButton>(sButtonFeedback[i].name);
            if (button)
            {
                button->setLabel(sButtonFeedback[i].original);
            }
            sButtonFeedback.erase(sButtonFeedback.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

// Flush Cache: the viewer's usual "clear cache" (it happens the next time the viewer starts).
static bool flush_cache_callback(const LLSD& notification, const LLSD& response)
{
    if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // Yes
    {
        gSavedSettings.setBOOL("PurgeCacheOnNextStartup", true);
        LLNotificationsUtil::add("CacheWillClear");
    }
    return false;
}

// Reindex Inv: deletes the saved copy of your inventory, so the next login loads it fresh from the server.
static int delete_inventory_cache_files()
{
    const std::string        cache_dir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "");
    static const char* const MASKS[]   = { "*.inv.llsd", "*.inv.gz", "*.inv" };

    std::vector<std::string> files;
    for (size_t i = 0; i < sizeof(MASKS) / sizeof(MASKS[0]); ++i)
    {
        LLDirIterator iter(cache_dir, MASKS[i]);
        std::string   name;
        while (iter.next(name))
        {
            files.push_back(gDirUtilp->add(cache_dir, name));
        }
    }
    int removed = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (LLFile::remove(files[i]) == 0)
        {
            ++removed;
        }
    }
    LL_INFOS("HyenaDebug") << "HYENA: inventory cache files removed: " << removed << LL_ENDL;
    return removed;
}

// Reset Voice: puts the voice audio devices and ear position back to their defaults.
static int reset_voice_settings()
{
    static const char* const NAMES[] = { "VoiceInputAudioDevice", "VoiceOutputAudioDevice", "VoiceEarLocation" };
    int                      reset   = 0;
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i)
    {
        if (gSavedSettings.controlExists(NAMES[i]))
        {
            gSavedSettings.getControl(NAMES[i])->resetToDefault(true);
            ++reset;
        }
    }
    LL_INFOS("HyenaDebug") << "HYENA: voice settings reset: " << reset << LL_ENDL;
    return reset;
}

// Ping Test: times a request to the Second Life status service. If that does not answer it tries the
// Second Life login server, and if that does not answer either it tries Google. The first one that answers
// gives the result; the button says which one it was when it was not the first.
static bool sPingRunning = false;

struct PingTarget
{
    const char* url;
    const char* tag; // shown after the time
};

static const PingTarget PING_TARGETS[] = {
    { "https://status.secondlifegrid.net/api/v2/status.json", "" },
    { "https://login.agni.lindenlab.com/cgi-bin/login.cgi", " (login)" },
    { "https://www.google.com/generate_204", " (web)" },
};

static void ping_coro(LLHandle<LLView> handle)
{
    struct Guard
    {
        ~Guard() { sPingRunning = false; }
    } guard;

    LLCore::HttpRequest::policy_t               policy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("HyenaPing", policy);
    LLCore::HttpRequest::ptr_t                  request = std::make_shared<LLCore::HttpRequest>();
    LLCore::HttpOptions::ptr_t                  options = std::make_shared<LLCore::HttpOptions>();
    options->setTimeout(5); // each attempt

    const size_t count = sizeof(PING_TARGETS) / sizeof(PING_TARGETS[0]);
    for (size_t i = 0; i < count; ++i)
    {
        LLTimer timer;
        timer.reset();
        LLSD               result  = adapter->getRawAndSuspend(request, PING_TARGETS[i].url, options);
        const int          ms      = (int)(timer.getElapsedTimeF32() * 1000.f);
        LLSD               results = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status  = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(results);

        // Any real HTTP answer (even an error page) proves the server was reached; only a failed
        // connection or a timeout counts as "no answer".
        const int  http_code = static_cast<int>(status.getType());
        const bool reached   = status || (http_code >= 100 && http_code <= 599);
        LL_INFOS("HyenaDebug") << "HYENA: ping " << PING_TARGETS[i].url << " -> " << ms << " ms, " << (reached ? "answered" : "no answer")
            << LL_ENDL;

        LLPanel* panel = dynamic_cast<LLPanel*>(handle.get());
        if (!panel)
        {
            return;
        }
        if (reached)
        {
            set_button_feedback(panel, "preflight_ping_btn", llformat("%d ms", ms) + PING_TARGETS[i].tag, 4.0);
            return;
        }
    }

    LLPanel* panel = dynamic_cast<LLPanel*>(handle.get());
    if (panel)
    {
        set_button_feedback(panel, "preflight_ping_btn", "No response", 4.0);
    }
}

static void start_ping(LLPanel* panel)
{
    if (sPingRunning)
    {
        return;
    }
    sPingRunning = true;
    set_button_feedback(panel, "preflight_ping_btn", "Pinging...", 20.0);
    LLCoros::instance().launch("HyenaPing", boost::bind(&ping_coro, panel->getHandle()));
}

// =====================================================================
//  DESTINATION GUIDE
// =====================================================================

// { "featured": [ {tag, name, title, region, slurl, description, avatars, notes, badge}, ... ],
//   "popular": [...], "editors": [...], "newcomer": [...] }
static LLSD sDestinations;
static int  sDestTab        = 0;     // which tab is lit
static int  sDestSelected   = 0;     // which of the three cards is shown big (the first one at start)
static bool sDestUserPicked = false; // card highlight only appears once the user has clicked a tab or card

static const int         DEST_TAB_COUNT                   = 4;
static const char* const DEST_TAB_KEYS[DEST_TAB_COUNT]    = { "featured", "popular", "editors", "newcomer" };
static const char* const DEST_TAB_BUTTONS[DEST_TAB_COUNT] = { "dest_tab_featured", "dest_tab_popular", "dest_tab_editors",
"dest_tab_newcomer" };
static const char* const DEST_TAB_BADGES[DEST_TAB_COUNT]  = { "FEATURED REGION", "TOP SPOT", "HYENA CURATED", "NEWCOMER FRIENDLY" };

static LLSD make_destination(const char* tag,
    const char* name,
    const char* title,
    const char* region,
    const char* slurl,
    const char* description,
    const char* avatars,
    const char* notes,
    const char* badge)
{
    LLSD d;
    d["tag"]         = tag;
    d["name"]        = name;
    d["title"]       = title;
    d["region"]      = region;
    d["slurl"]       = slurl;
    d["description"] = description;
    d["avatars"]     = avatars;
    d["notes"]       = notes;
    d["badge"]       = badge;
    return d;
}

// Used when there is no hyena_destinations.xml. These three are only samples from the design mock-up
// (the regions do not exist), so replace them with real places in the file.
static LLSD builtin_destinations()
{
    LLSD all      = LLSD::emptyMap();
    LLSD featured = LLSD::emptyArray();
    featured.append(make_destination("CYBERPUNK", "Neon City Plaza", "Neon City Cyberpunk Plaza", "NeonPlaza",
        "secondlife://NeonPlaza/128/128/25",
        "Next-generation dual-PBR reflection probes, volumetric rain shaders, and live synthwave DJ club.",
        "28 Avatars", "Smooth Physics", "FEATURED PBR REGION"));
    featured.append(make_destination("MUSIC CLUB", "Midnight Lounge", "Midnight Lounge", "MidnightIsle",
        "secondlife://MidnightIsle/128/128/25", "A late-night club with live DJs and a neon skyline.",
        "12 Avatars", "Live Music", "MUSIC CLUB"));
    featured.append(
        make_destination("NATURE PBR", "Serenity Sanctuary", "Serenity Sanctuary", "SerenityPBR", "secondlife://SerenityPBR/128/128/25",
            "Quiet gardens, waterfalls and realistic PBR water and foliage.", "6 Avatars", "Relaxed", "NATURE PBR"));
    all["featured"] = featured;

    // PLACEHOLDERS: sample entries so the other tabs have something to show. The regions do not exist,
    // replace them with real places (or put real ones in hyena_destinations.xml).
    LLSD popular = LLSD::emptyArray();
    popular.append(
        make_destination("SHOPPING", "Aurora Mall", "Aurora Shopping District", "AuroraMall", "secondlife://AuroraMall/128/128/25",
            "A huge open-air mall with hundreds of fashion, home and avatar stores.", "64 Avatars", "Busy", "TOP SPOT"));
    popular.append(make_destination(
        "SOCIAL", "Sunset Boardwalk", "Sunset Boardwalk Beach", "SunsetBoardwalk", "secondlife://SunsetBoardwalk/128/128/25",
        "A sunny beach town with shops, cafes, dance floors and boat rides.", "41 Avatars", "Friendly", "TOP SPOT"));
    popular.append(make_destination(
        "ADVENTURE", "Dragon's Reach", "Dragon's Reach Adventure Land", "DragonsReach", "secondlife://DragonsReach/128/128/25",
        "Explore caves, castles and quests across a huge fantasy landscape.", "33 Avatars", "Quests", "TOP SPOT"));
    all["popular"] = popular;

    LLSD editors = LLSD::emptyArray();
    editors.append(make_destination("HYENA CURATED", "Crystal Gardens", "Crystal Gardens Botanical Park", "CrystalGardens",
        "secondlife://CrystalGardens/128/128/25",
        "A hand-built botanical park with glass domes, winding paths and quiet picnic spots.", "14 Avatars",
        "Beautiful Build", "HYENA CURATED"));
    editors.append(make_destination(
        "HYENA CURATED", "Lantern Harbor", "Lantern Harbor Fishing Village", "LanternHarbor", "secondlife://LanternHarbor/128/128/25",
        "A cozy harbor town lit by paper lanterns, with boats to ride and cafes to explore.", "19 Avatars", "Photo Spot", "HYENA CURATED"));
    editors.append(make_destination("HYENA CURATED", "Aether Observatory", "Aether Observatory Sky Garden", "AetherObservatory",
        "secondlife://AetherObservatory/128/128/25",
        "A floating observatory above the clouds with a planetarium and stargazing decks.", "9 Avatars",
        "Great Views", "HYENA CURATED"));
    all["editors"] = editors;

    LLSD newcomer = LLSD::emptyArray();
    newcomer.append(make_destination("WELCOME", "Welcome Isle", "Welcome Isle Starter Hub", "WelcomeIsle",
        "secondlife://WelcomeIsle/128/128/25",
        "A friendly hub with helpers, free starter items and guides to your first steps.", "46 Avatars",
        "Helpers On Duty", "NEWCOMER FRIENDLY"));
    newcomer.append(make_destination(
        "COMMUNITY", "Friendly Fields", "Friendly Fields Community Park", "FriendlyFields", "secondlife://FriendlyFields/128/128/25",
        "A relaxed community park with games, chat circles and a welcoming crowd.", "27 Avatars", "Beginner Safe", "NEWCOMER FRIENDLY"));
    newcomer.append(make_destination(
        "LEARNING", "Builders Academy", "Builders Academy Learning Center", "BuildersAcademy", "secondlife://BuildersAcademy/128/128/25",
        "Free classes and practice areas for building, scripting and avatar styling.", "21 Avatars", "Free Classes", "NEWCOMER FRIENDLY"));
    all["newcomer"] = newcomer;
    return all;
}

// The destinations come from hyena_destinations.xml: first the user settings folder, then app_settings.
static void load_counts();
static void load_destinations()
{
    sDestinations = builtin_destinations();
    load_counts();

    std::vector<std::string> paths;
    paths.push_back(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "hyena_destinations.xml"));
    paths.push_back(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "hyena_destinations.xml"));
    for (size_t i = 0; i < paths.size(); ++i)
    {
        llifstream file;
        file.open(paths[i].c_str());
        if (!file.is_open())
        {
            continue;
        }
        // Read the whole file and turn any bare "&" (as in "Music & Radio") into "&amp;" first,
        // so it can be typed as a normal ampersand. Real XML entities (&amp; &lt; &#38; ...) are left alone.
        std::stringstream raw_text;
        raw_text << file.rdbuf();
        file.close();
        const std::string source = raw_text.str();
        std::string       fixed;
        fixed.reserve(source.size() + 32);
        for (size_t c = 0; c < source.size(); ++c)
        {
            if (source[c] == '&')
            {
                const size_t semi   = source.find(';', c);
                const bool   entity = (semi != std::string::npos && semi - c <= 8 && source.find_first_of(" \t\r\n<&", c + 1) > semi);
                if (!entity)
                {
                    fixed += "&amp;";
                    continue;
                }
            }
            fixed += source[c];
        }
        std::istringstream fixed_stream(fixed);
        LLSD               loaded;
        LLSDSerialize::fromXML(loaded, fixed_stream);
        if (loaded.isMap())
        {
            sDestinations = loaded;
            LL_INFOS("HyenaDebug") << "HYENA: destinations loaded from " << paths[i] << LL_ENDL;
            return;
        }
        LL_WARNS("HyenaDebug") << "HYENA: destinations file could not be read: " << paths[i] << LL_ENDL;
    }
    LL_INFOS("HyenaDebug") << "HYENA: no hyena_destinations.xml found, using the built-in samples" << LL_ENDL;
}

// The entry at position `index` of a list, or an empty value
static LLSD dest_at(const LLSD& list, int index)
{
    int i = 0;
    for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it, ++i)
    {
        if (i == index)
        {
            return *it;
        }
    }
    return LLSD();
}

// =====================================================================
//  "LAST CHECKED" AVATAR COUNTS
//  Once you are logged in, the viewer asks the grid (the same way the world map does) how many avatars
//  are in each listed region, every thirty minutes. The numbers are saved with the time they were taken in
//  hyena_destination_counts.xml (user settings folder), and the launcher shows them the next time it
//  opens as "12 avatars - checked 3h ago". They are never shown as live.
// =====================================================================
static LLSD sDestCounts; // region name (lower case) -> { count, time }

static std::string counts_file()
{ return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "hyena_destination_counts.xml"); }

static void load_counts()
{
    sDestCounts = LLSD::emptyMap();
    llifstream file;
    file.open(counts_file().c_str());
    if (file.is_open())
    {
        LLSD loaded;
        LLSDSerialize::fromXML(loaded, file);
        file.close();
        if (loaded.isMap())
        {
            sDestCounts = loaded;
        }
    }
}

static void save_counts()
{
    llofstream file;
    file.open(counts_file().c_str());
    if (file.is_open())
    {
        LLSDSerialize::toPrettyXML(sDestCounts, file);
        file.close();
    }
}

// The region name of a destination, taken from its SLURL (or its "region" field), lower case.
static std::string region_key_of(const LLSD& d)
{
    std::string source = d["slurl"].asString();
    if (source.empty())
    {
        source = d["region"].asString();
    }
    std::string slurl;
    if (!build_region_slurl(source, slurl))
    {
        return "";
    }
    const std::string prefix = "secondlife://";
    std::string       name   = slurl.substr(prefix.size());
    const size_t      slash  = name.find('/');
    if (slash != std::string::npos)
    {
        name = name.substr(0, slash);
    }
    name = LLURI::unescape(name);
    LLStringUtil::toLower(name);
    return name;
}

static std::string ago_text(double then)
{
    const double secs = (double)time(NULL) - then;
    if (secs < 90.0)
        return "just now";
    if (secs < 3600.0)
        return llformat("%d min ago", (int)(secs / 60.0));
    if (secs < 86400.0)
        return llformat("%d h ago", (int)(secs / 3600.0));
    return llformat("%d d ago", (int)(secs / 86400.0));
}

// Runs every frame (even while the launcher is hidden). Three steps, thirty minutes apart:
// ask for the regions, ask each one for its avatars, then read the answers and save them.
static void poll_destination_counts()
{
    static int                      stage        = 0;
    static bool                     was_in_world = false;
    static LLTimer                  timer;
    static std::vector<std::string> regions;

    const bool in_world = (LLStartUp::getStartupState() == STATE_STARTED);
    if (!in_world)
    {
        was_in_world = false;
        stage        = 0;
        return;
    }
    if (!was_in_world)
    {
        was_in_world = true; // first look 30 seconds after arriving, so login is not slowed down
        stage        = 0;
        timer.reset();
        timer.setTimerExpirySec(30.f);
    }
    if (!timer.hasExpired())
    {
        return;
    }

    LLWorldMap* map = LLWorldMap::getInstance();
    if (stage == 0)
    {
        regions.clear();
        std::set<std::string> seen;
        for (int t = 0; t < DEST_TAB_COUNT; ++t)
        {
            const LLSD list = sDestinations[DEST_TAB_KEYS[t]];
            if (!list.isArray())
            {
                continue;
            }
            for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
            {
                const std::string key = region_key_of(*it);
                if (!key.empty() && seen.insert(key).second && regions.size() < 40)
                {
                    regions.push_back(key);
                    LLWorldMapMessage::getInstance()->sendNamedRegionRequest(key);
                }
            }
        }
        stage = 1;
        timer.setTimerExpirySec(8.f);
    }
    else if (stage == 1)
    {
        for (size_t i = 0; i < regions.size(); ++i)
        {
            LLSimInfo* sim = map->simInfoFromName(regions[i]);
            if (sim && !sim->isDown())
            {
                LLWorldMapMessage::getInstance()->sendItemRequest(MAP_ITEM_AGENT_LOCATIONS, sim->getHandle());
            }
        }
        stage = 2;
        timer.setTimerExpirySec(8.f);
    }
    else
    {
        bool changed = false;
        for (size_t i = 0; i < regions.size(); ++i)
        {
            LLSimInfo* sim = map->simInfoFromName(regions[i]);
            if (sim && !sim->isDown())
            {
                LLSD entry;
                entry["count"]          = (S32)sim->getAgentCount();
                entry["time"]           = (F64)time(NULL);
                sDestCounts[regions[i]] = entry;
                changed                 = true;
            }
        }
        if (changed)
        {
            save_counts();
            LL_INFOS("HyenaDebug") << "HYENA: avatar counts saved for " << regions.size() << " regions" << LL_ENDL;
        }
        stage = 0;
        timer.setTimerExpirySec(1800.f);
    }
}

// Pictures for the big preview box. A destination's "picture" is a file name (jpg, png, bmp or tga).
// It is looked for in the folder  hyena_destinations  inside the user settings folder first, then inside
// app_settings. A full path also works. Each file is loaded once and kept.
static std::map<std::string, LLUIImagePtr> sDestPictures;

static LLUIImagePtr load_destination_picture(const std::string& wanted, std::string& problem)
{
    problem.clear();
    std::string file = wanted;
    LLStringUtil::trim(file);
    // "Copy as path" in Windows wraps the path in quotes; drop them
    while (file.size() >= 2 && (file[0] == '"' || file[0] == '\'') && file[file.size() - 1] == file[0])
    {
        file = file.substr(1, file.size() - 2);
        LLStringUtil::trim(file);
    }
    for (size_t i = 0; i < file.size(); ++i)
    {
        if (file[i] == '\\')
        {
            file[i] = '/';
        }
    }
    const std::string full_path = file; // as written (may be a full path)
    const size_t      slash     = file.find_last_of('/');
    if (slash != std::string::npos)
    {
        file = file.substr(slash + 1); // just the file name, searched for in the hyena_destinations folders
    }

    std::map<std::string, LLUIImagePtr>::iterator found = sDestPictures.find(file);
    if (found != sDestPictures.end() && found->second.notNull())
    {
        return found->second;
    }

    // Try the name as written, then with the usual picture endings added (when the ending was left off)
    std::vector<std::string> names;
    names.push_back(file);
    if (file.find('.') == std::string::npos)
    {
        names.push_back(file + ".jpg");
        names.push_back(file + ".png");
        names.push_back(file + ".jpeg");
        names.push_back(file + ".tga");
        names.push_back(file + ".bmp");
    }

    std::string path, user_folder, app_folder;
    for (size_t i = 0; i < names.size() && path.empty(); ++i)
    {
        const std::string user_copy = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "hyena_destinations", names[i]);
        const std::string app_copy  = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "hyena_destinations", names[i]);
        if (i == 0)
        {
            user_folder = user_copy;
            app_folder  = app_copy;
        }
        if (i == 0 && gDirUtilp->fileExists(full_path))
        {
            path = full_path;
        }
        else if (gDirUtilp->fileExists(names[i]))
        {
            path = names[i];
        }
        else if (gDirUtilp->fileExists(user_copy))
        {
            path = user_copy;
        }
        else if (gDirUtilp->fileExists(app_copy))
        {
            path = app_copy;
        }
    }

    LLUIImagePtr result;
    if (path.empty())
    {
        LL_WARNS("HyenaDebug") << "HYENA: destination picture not found: " << file << " (looked in " << app_folder << " and " << user_folder
            << ")" << LL_ENDL;
        problem = "PICTURE NOT FOUND: " + file + "\nLooked here: " + app_folder;
        return result;
    }

    LLPointer<LLImageFormatted> image = LLImageFormatted::createFromExtension(path);
    LLPointer<LLImageRaw>       raw   = new LLImageRaw;
    if (image.notNull() && image->load(path) && image->decode(raw, 0.0f) && raw->getData() != NULL)
    {
        // Same recipe the viewer uses for its own local bitmaps: GL textures want power-of-two sizes
        raw->biasedScaleToPowerOfTwo(1024);
        // Same recipe the viewer uses for pictures it builds from memory (getImageFromMemory):
        // make the texture straight from the decoded picture and tell the viewer never to discard it
        LLPointer<LLViewerFetchedTexture> texture = new LLViewerFetchedTexture(raw, FTT_LOCAL_FILE, false);
        if (texture.notNull())
        {
            texture->ref(); // keep it alive for the whole session
            texture->dontDiscard();
            texture->setNoDelete();
            texture->setAddressMode(LLTexUnit::TAM_CLAMP);
            texture->setBoostLevel(LLGLTexture::BOOST_PREVIEW);
            result              = new LLUIImage(file, texture);
            sDestPictures[file] = result;
            LL_INFOS("HyenaDebug") << "HYENA: destination picture loaded: " << path << " (" << raw->getWidth() << "x" << raw->getHeight()
                << ", " << (S32)raw->getComponents() << " channels)" << LL_ENDL;
            return result;
        }
        LL_WARNS("HyenaDebug") << "HYENA: could not make a texture from: " << path << LL_ENDL;
    }
    LL_WARNS("HyenaDebug") << "HYENA: destination picture could not be read: " << path << LL_ENDL;
    problem = "PICTURE COULD NOT BE READ: " + path + "\nUse a normal .jpg or .png file.";
    return result;
}

// Fills the Destination Guide: the lit tab, the big destination, and the three cards at the bottom.
static void show_destinations(LLPanel* panel)
{
    const LLSD list  = sDestinations[DEST_TAB_KEYS[sDestTab]];
    const int  count = list.isArray() ? (int)list.size() : 0;
    if (sDestSelected >= count)
    {
        sDestSelected = 0;
    }

    for (int i = 0; i < DEST_TAB_COUNT; ++i)
    {
        LLButton* tab = panel->findChild<LLButton>(DEST_TAB_BUTTONS[i]);
        if (tab)
        {
            tab->setToggleState(i == sDestTab);
        }
    }

    std::string title, description, avatars, notes, badge, picture;
    if (count > 0)
    {
        const LLSD d = dest_at(list, sDestSelected);
        title        = d["title"].asString().empty() ? d["name"].asString() : d["title"].asString();
        description  = d["description"].asString();
        avatars      = d["avatars"].asString();
        notes        = d["notes"].asString();
        badge        = d["badge"].asString().empty() ? std::string(DEST_TAB_BADGES[sDestTab]) : d["badge"].asString();
        picture      = d["picture"].asString();

        // Newest saved count for this region wins over typed text, and always says when it was taken
        const std::string key = region_key_of(d);
        if (!key.empty() && sDestCounts.has(key))
        {
            const S32 n    = sDestCounts[key]["count"].asInteger();
            const F64 when = sDestCounts[key]["time"].asReal();
            if ((F64)time(NULL) - when > 7.0 * 86400.0)
            {
                // too old to be useful: do not show the number at all
                avatars = "Not checked recently";
            }
            else
            {
                avatars = llformat("%d avatar%s - checked %s", n, n == 1 ? "" : "s", ago_text(when).c_str());
            }
        }
    }
    else
    {
        title       = "No destinations yet";
        description = "Add entries for this tab to hyena_destinations.xml.";
    }
    {
        std::string flat;
        bool        space = false;
        for (size_t c = 0; c < description.size(); ++c)
        {
            const char ch = description[c];
            if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t')
            {
                space = !flat.empty();
            }
            else
            {
                if (space)
                {
                    flat += ' ';
                    space = false;
                }
                flat += ch;
            }
        }
        description = flat;
    }
    // Nothing known yet: say so plainly instead of leaving the line blank
    const bool never = avatars.empty();
    if (never && count > 0)
    {
        avatars = "Not checked yet";
    }
    const bool unchecked = never || avatars == "Not checked recently"; // shown grey, without the green dot
    LLStringUtil::toUpper(title);
    LLStringUtil::toUpper(badge);
    set_label(panel, "dest_title", title);
    set_label(panel, "dest_desc", description);
    {
        const LLColor4 green(0.20f, 0.90f, 0.50f, 1.0f);
        const LLColor4 grey(0.62f, 0.70f, 0.78f, 1.0f);
        set_label(panel, "dest_avatars", avatars, unchecked ? &grey : &green);
    }
    set_label(panel, "dest_notes", notes.empty() ? notes : "- " + notes);
    set_label(panel, "dest_badge_text", badge);

    // The picture fills the big box; without one the placeholder text shows instead
    LLUIImagePtr picture_image;
    std::string  picture_problem;
    if (!picture.empty())
    {
        picture_image = load_destination_picture(picture, picture_problem);
    }
    LLIconCtrl* picture_icon = panel->findChild<LLIconCtrl>("dest_preview_img");
    if (picture_icon)
    {
        if (picture_image.notNull())
        {
            picture_icon->setImage(picture_image);
        }
        picture_icon->setVisible(picture_image.notNull());
    }
    LLView* preview_text = panel->findChildView("dest_preview_text");
    if (preview_text)
    {
        preview_text->setVisible(picture_image.isNull());
    }
    // When a picture was asked for but could not be shown, say why right in the box
    set_label(panel, "dest_preview_text", picture_problem.empty() ? std::string("[ PBR REGION RENDER PREVIEW ]") : picture_problem);

    // Keep the note right next to the count: size the count line to its text, then start the note just after it
    {
        LLTextBox* av   = panel->findChild<LLTextBox>("dest_avatars");
        LLTextBox* note = panel->findChild<LLTextBox>("dest_notes");
        if (av && note && note->getParent())
        {
            const LLRect av_rect = av->getRect();
            av->reshape(400, av_rect.getHeight());
            const S32 av_width = av->getTextPixelWidth() + 4;
            av->reshape(av_width, av_rect.getHeight());
            const LLRect note_rect  = note->getRect();
            const S32    note_left  = av_rect.mLeft + av_width + 8;
            const S32    note_width = llmax(20, note->getParent()->getRect().getWidth() - 140 - note_left);
            note->setOrigin(note_left, note_rect.mBottom);
            note->reshape(note_width, note_rect.getHeight());
        }
    }

    LLIconCtrl* dot = panel->findChild<LLIconCtrl>("dest_stats_dot");
    if (dot)
    {
        dot->setVisible(!avatars.empty() && !unchecked);
    }
    LLView* badge_bg = panel->findChildView("dest_badge_bg");
    if (badge_bg)
    {
        badge_bg->setVisible(!badge.empty());

        // Make the badge hug its text (8 px of padding each side), like the reference design
        LLTextBox* badge_text = panel->findChild<LLTextBox>("dest_badge_text");
        if (badge_text && !badge.empty())
        {
            badge_text->reshape(400, badge_text->getRect().getHeight()); // wide enough that nothing is cut before measuring
            const S32 badge_width = badge_text->getTextPixelWidth() + 16;
            badge_text->reshape(badge_width, badge_text->getRect().getHeight());
            badge_bg->reshape(badge_width, badge_bg->getRect().getHeight());
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        const bool  has = (i < count);
        LLSD        d   = has ? dest_at(list, i) : LLSD();
        std::string tag = d["tag"].asString();
        LLStringUtil::toUpper(tag);
        const std::string region = d["region"].asString();
        set_label(panel, llformat("dest_card%d_tag", i + 1), tag);
        set_label(panel, llformat("dest_card%d_name", i + 1), d["name"].asString());
        set_label(panel, llformat("dest_card%d_region", i + 1), region.empty() ? region : "Region: " + region);

        LLIconCtrl* selected = panel->findChild<LLIconCtrl>(llformat("dest_card%d_sel", i + 1));
        if (selected)
        {
            selected->setVisible(has && sDestUserPicked && i == sDestSelected);
        }
        LLButton* card = panel->findChild<LLButton>(llformat("dest_card%d_btn", i + 1));
        if (card)
        {
            card->setEnabled(has);
        }
    }
}

// SET AS TARGET: turns the Start Location into "Custom Region" with this destination typed in.
static void set_destination_target(LLPanel* panel, LLComboBox* combo)
{
    const LLSD  list   = sDestinations[DEST_TAB_KEYS[sDestTab]];
    const LLSD  d      = dest_at(list, sDestSelected);
    std::string target = d["slurl"].asString();
    if (target.empty())
    {
        target = d["region"].asString();
    }
    LLLineEditor* region_edit = panel->findChild<LLLineEditor>("region_edit");
    if (target.empty() || !combo || !region_edit)
    {
        set_button_feedback(panel, "dest_target_btn", "NOTHING TO SET");
        return;
    }
    region_edit->setText(target);
    combo->setValue(LLSD("CustomRegion"));
    update_region_box(panel, combo, false);
    set_button_feedback(panel, "dest_target_btn", "TARGET SET");
    LL_INFOS("HyenaDebug") << "HYENA: destination target set: " << target << LL_ENDL;
}

#if LL_WINDOWS
// ---------------------------------------------------------------------
//  CUSTOM TITLE BAR (login screen AND in-world)
//  The Windows title bar is switched off and the 34 px strip at the top takes its place: the compact logo box,
//  the viewer menus, the status items (shifted left) and our own minimize / maximize / close buttons.
//  On the login screen the bar parts live inside the launcher; in-world they move into status_bar_container.
// ---------------------------------------------------------------------
// the small legacy size for the location text, so the location takes less room in the bar
static LLFontGL* hyena_loc_font()
{
    return LLFontGL::getFontSansSerifSmall(); // a little bigger than the old LSmall
}
static const S32 HYENA_BAR_HEIGHT      = 34;  // must match the strip in floater_hyena_launcher.xml
static const S32 HYENA_STRIP_HEIGHT    = 19;  // height of the viewer's own menu strip (main_view.xml)
static const S32 HYENA_MENU_LEFT_LOGIN = 160; // login screen: menus start right of the logo box with the full name
static const S32 HYENA_MENU_LEFT_WORLD = 50;  // in-world: menus start right of the logo-only box
static const S32 HYENA_BOX_WIDTH_LOGIN = 152; // logo box with "PROJECT HYENA"
static const S32 HYENA_BOX_WIDTH_WORLD = 40;  // logo box with just the logo
extern const std::string HYENA_STAGE;
extern const std::string HYENA_VERSION;   // defined in llappviewer.cpp
extern const std::string HYENA_BUILD_TAG; // defined in llappviewer.cpp
static const S32 HYENA_BUTTONS_WIDTH   = 140; // the three 46 px buttons plus a little margin
static const S32 HYENA_FPS_WIDTH       = 66;  // room for the FPS number right before the window buttons
static const S32 HYENA_STATUS_HEIGHT   = 19;  // height of the viewer's status bar (clock, volume ...)

static bool    sSearchOpen       = false; // the menu search box is shown (opened with the magnifier button)
static bool    sSearchJustOpened = false;
static LLTimer sSearchOpenTimer;

// About Land for the parcel the avatar is standing on RIGHT NOW. The viewer only picks the current parcel when
// nothing is selected, so without this it keeps showing the parcel that was selected before.
// Dark launcher look for the pieces inside a status-bar pop-up: light text, cyan push buttons (icon-only buttons keep
// their own pictures).
static void hyena_theme_popup(LLView* root)
{
    const LLView::child_list_t* kids = root->getChildList();
    if (!kids)
    {
        return;
    }
    for (LLView::child_list_const_iter_t it = kids->begin(); it != kids->end(); ++it)
    {
        LLView* v = *it;
        if (!v)
        {
            continue;
        }
        if (LLButton* b = dynamic_cast<LLButton*>(v))
        {
            if (!b->getLabelUnselected().empty())
            {
                LLUIImagePtr cyan = LLUI::getUIImage("Button_Cyan");
                b->setImageUnselected(cyan);
                b->setImageSelected(cyan);
                b->setImageHoverUnselected(cyan);
                b->setImageHoverSelected(cyan);
                b->setImagePressed(cyan);
                b->setUnselectedLabelColor(LLColor4(0.f, 0.05f, 0.08f, 1.f));
                b->setSelectedLabelColor(LLColor4(0.f, 0.05f, 0.08f, 1.f));
            }
        }
        else if (LLTextBox* t = dynamic_cast<LLTextBox*>(v))
        {
            t->setColor(LLColor4(0.85f, 0.93f, 0.97f, 1.f));
            t->setReadOnlyColor(LLColor4(0.85f, 0.93f, 0.97f, 1.f));
        }
        hyena_theme_popup(v);
    }
}

static void open_about_land()
{
    LLViewerParcelMgr::getInstance()->selectParcelAt(gAgent.getPositionGlobal());
    LLFloaterReg::showInstance("about_land");
}

// ---- two small in-world touches (kept cheap: views are looked up once, then only compared each frame) ----
// 1) notification button (top right, under the bar): the envelope stays, a little bigger, with the count in small text
//    underneath it, in one taller pill. 2) a dark rounded box behind the favorites (landmarks) bar.
static LLButton*   sWellBtn   = NULL;
static LLIconCtrl* sWellIcon  = NULL;
static LLTextBox*  sWellCount = NULL;
static LLView*     sWellPanel = NULL; // the viewer's old slot for the box: the notification list docks under it
static LLView*     sFavBar    = NULL;
static LLView*     sFavBtn    = NULL;
static LLView*     sNavHolder = NULL;
static int         sWellLit   = -1;

// forget the cached views (they are rebuilt at login)
static void hyena_reset_ui_touches()
{
    sWellBtn   = NULL;
    sWellPanel = NULL;
    sWellIcon  = NULL;
    sWellCount = NULL;
    sFavBar    = NULL;
    sFavBtn    = NULL;
    sNavHolder = NULL;
    sWellLit   = -1;
}

// Camera controls window: same navy look as the rest (plain navy instead of the grey skin, light text, cyan buttons)
static void hyena_camera_theme()
{
    LLFloater* cam = LLFloaterReg::findInstance("camera");
    if (!cam || !cam->getVisible())
    {
        return;
    }
    cam->setBackgroundImage(NULL);
    cam->setTransparentImage(NULL);
    cam->setBackgroundColor(LLColor4(0.075f, 0.118f, 0.165f, 1.f));
    cam->setBackgroundOpaque(true);
    hyena_theme_popup(cam);
}

// Conversations window: a plain chat window while only Nearby Chat is open; the left conversation list (and its
// buttons) appear only once there is a person/group chat. No mic button. Runs about once a second.
static void hyena_conversations()
{
    LLFloater* fl = LLFloaterReg::findInstance("im_container");
    if (!fl)
    {
        return;
    }
    static bool    sThemed = false;
    LLLayoutPanel* left    = fl->findChild<LLLayoutPanel>("conversations_layout_panel", true);
    if (!left)
    {
        return;
    }
    if (LLButton* mic = fl->findChild<LLButton>("speak_btn", true))
    {
        mic->setVisible(false);
    }
    // Nearby chat does not need the call (phone) button; no collapse or tear-off buttons anywhere (close is the title bar X)
    {
        static const char* always_hidden[] = { "expand_collapse_btn", "tear_off_btn", "stub_collapse_btn" };
        static const char* nearby_hidden[] = { "voice_call_btn" };
        std::function<void(LLView*, const char* const*, size_t)> hide_named = [&](LLView* root, const char* const* names, size_t n)
            {
                const LLView::child_list_t* kids = root->getChildList();
                if (!kids)
                {
                    return;
                }
                for (LLView::child_list_const_iter_t it = kids->begin(); it != kids->end(); ++it)
                {
                    LLView* v = *it;
                    if (!v)
                    {
                        continue;
                    }
                    for (size_t k = 0; k < n; ++k)
                    {
                        if (v->getName() == names[k] && v->getVisible())
                        {
                            v->setVisible(false);
                        }
                    }
                    hide_named(v, names, n);
                }
            };
        hide_named(fl, always_hidden, 3);
        // the X (end conversation) sits at the far right of each toolbar row now that the buttons beside it are gone
        std::function<void(LLView*)> move_close = [&](LLView* root)
            {
                const LLView::child_list_t* kids = root->getChildList();
                if (!kids)
                {
                    return;
                }
                for (LLView::child_list_const_iter_t it = kids->begin(); it != kids->end(); ++it)
                {
                    LLView* v = *it;
                    if (!v)
                    {
                        continue;
                    }
                    if (v->getName() == "close_btn" && v->getParent() && v->getParent()->getName() == "toolbar_panel")
                    {
                        const LLRect pr = v->getParent()->getLocalRect();
                        LLRect       r  = v->getRect();
                        const S32    w  = r.getWidth();
                        if (r.mRight != pr.mRight - 4)
                        {
                            r.mRight = pr.mRight - 4;
                            r.mLeft  = r.mRight - w;
                            v->setRect(r);
                        }
                    }
                    move_close(v);
                }
            };
        move_close(fl);
        // the little phone on the "(Nearby chat)" entry of the list
        std::function<void(LLView*)> hide_nearby_phone = [&](LLView* root)
            {
                const LLView::child_list_t* kids = root->getChildList();
                if (!kids)
                {
                    return;
                }
                for (LLView::child_list_const_iter_t it = kids->begin(); it != kids->end(); ++it)
                {
                    LLView* v = *it;
                    if (!v)
                    {
                        continue;
                    }
                    if (v->getName() == "conversation_list_item")
                    {
                        LLView* ni = v->findChildView("nearby_chat_icon", true);
                        LLView* ph = v->findChildView("call_icon_panel", true);
                        if (ni && ni->getVisible() && ph && ph->getVisible())
                        {
                            ph->setVisible(false);
                        }
                    }
                    else
                    {
                        hide_nearby_phone(v);
                    }
                }
            };
        if (LLView* lp = fl->findChildView("conversations_list_panel", true))
        {
            hide_nearby_phone(lp);
        }
        LLFloater* nb = LLFloaterReg::findInstance("nearby_chat");
        if (nb)
        {
            if (LLView* bar = nb->findChildView("toolbar_panel", true))
            {
                hide_named(bar, nearby_hidden, 1);
            }
        }
        // "CONVERSATIONS - Nearby": the title says which conversation is showing
        std::string which;
        {
            const LLFloaterReg::const_instance_list_t& open_chats = LLFloaterReg::getFloaterList("impanel");
            for (LLFloaterReg::const_instance_list_t::const_iterator it = open_chats.begin(); it != open_chats.end(); ++it)
            {
                if (*it && (*it)->isInVisibleChain())
                {
                    which = (*it)->getTitle();
                    break;
                }
            }
            if (which.empty() && nb && nb->isInVisibleChain())
            {
                which = "Nearby";
            }
        }
        const std::string want_title = which.empty() ? std::string("CONVERSATIONS") : ("CONVERSATIONS - " + which);
        if (fl->getTitle() != want_title)
        {
            fl->setTitle(want_title);
        }
    }
    const LLFloaterReg::const_instance_list_t& chats = LLFloaterReg::getFloaterList("impanel");
    // true = the list (Nearby on top) is always there, like the second screenshot; false = only once a person/group chat exists
    static const bool HYENA_ALWAYS_SHOW_CHAT_LIST = true;
    const bool        has_chats                   = HYENA_ALWAYS_SHOW_CHAT_LIST || !chats.empty();
    if (left->getVisible() != has_chats)
    {
        left->setVisible(has_chats);
    }
    if (!sThemed)
    {
        sThemed = true;
        const LLColor4 navy(0.047f, 0.078f, 0.114f, 1.f);
        // the title bar ("CONVERSATIONS") and window body: plain navy instead of the grey skin image
        fl->setBackgroundImage(NULL);
        fl->setTransparentImage(NULL);
        // a slightly lighter navy for the title bar and frame, so the header reads as a header (the panels inside stay darker)
        fl->setBackgroundColor(LLColor4(0.098f, 0.153f, 0.212f, 1.f));
        fl->setBackgroundOpaque(true);
        left->setBackgroundVisible(true);
        left->setBackgroundOpaque(true);
        left->setBackgroundColor(navy);
        left->setBorderVisible(false);
        if (LLLayoutPanel* msgs = fl->findChild<LLLayoutPanel>("messages_layout_panel", true))
        {
            msgs->setBackgroundVisible(true);
            msgs->setBackgroundOpaque(true);
            msgs->setBackgroundColor(navy);
            msgs->setBorderVisible(false);
        }
        LL_INFOS("HyenaDebug") << "HYENA: conversations window themed" << LL_ENDL;
    }
}

static void hyena_ui_touches(LLView* root)
{
    static U32 sFrame = 0;
    if (!root)
    {
        return;
    }
    // look for the views at most once a second until they are found
    ++sFrame;
    if ((sFrame % 30) == 7)
    {
        hyena_conversations();
        hyena_camera_theme();
    }
    // while the "Loading world" screen is up, these two must not show on top of it
    const bool loading = gViewerWindow->getProgressView() && gViewerWindow->getProgressView()->getVisible();
    if (loading)
    {
        if (sWellBtn && sWellBtn->getParent() && sWellBtn->getParent()->getVisible())
        {
            sWellBtn->getParent()->setVisible(false);
        }
        if (sNavHolder && sNavHolder->getVisible())
        {
            sNavHolder->setVisible(false);
        }
        return;
    }
    if ((!sWellBtn || !sFavBar) && (sFrame % 60) == 1)
    {
        if (!sWellBtn)
        {
            LLView*   well = root->findChildView("notification_well", true);
            LLButton* b    = well ? well->findChild<LLButton>("Unread", true) : NULL;
            if (b)
            {
                static const S32 WELL_W = 46;
                b->setImageOverlay(std::string());                        // we draw our own bigger envelope
                b->setUnselectedLabelColor(LLColor4(0.f, 0.f, 0.f, 0.f)); // the number is shown under the envelope instead
                b->setSelectedLabelColor(LLColor4(0.f, 0.f, 0.f, 0.f));
                sWellIcon  = b->findChild<LLIconCtrl>("hyena_well_icon", false);
                sWellCount = b->findChild<LLTextBox>("hyena_well_count", false);
                if (!sWellIcon)
                {
                    LLIconCtrl::Params ip;
                    ip.name("hyena_well_icon");
                    ip.rect(LLRect(0, 19, 26, 0));
                    ip.image(LLUI::getUIImage("Hyena_Envelope").get());
                    ip.mouse_opaque(false);
                    sWellIcon = LLUICtrlFactory::create<LLIconCtrl>(ip);
                    b->addChild(sWellIcon);
                }
                if (!sWellCount)
                {
                    LLTextBox::Params tp;
                    tp.name("hyena_well_count");
                    tp.rect(LLRect(0, 12, WELL_W, 0));
                    tp.font(LLFontGL::getFontSansSerifSmall());
                    tp.font_halign(LLFontGL::HCENTER);
                    tp.mouse_opaque(false);
                    sWellCount = LLUICtrlFactory::create<LLTextBox>(tp);
                    b->addChild(sWellCount);
                }
                sWellBtn   = b;
                sWellPanel = root->findChildView("notification_well_panel", true);
                // log only: the notification list opens docked at its normal (old) place, which we leave alone
                b->setClickedCallback(
                    [](LLUICtrl*, const LLSD&)
                    {
                        LLFloater* f = LLFloaterReg::findInstance("notification_well_window");
                        LL_INFOS("HyenaDebug") << "HYENA: notification box clicked, list exists=" << (int)(f != NULL)
                            << " visible=" << (f ? (int)f->getVisible() : -1) << LL_ENDL;
                    });
                // The box lives in a small container that clips it. Move it up to the window itself so the whole box shows.
                if (well && well->getParent() != root)
                {
                    LLView* old = well->getParent();
                    if (old)
                    {
                        old->removeChild(well);
                    }
                    root->addChild(well);
                }
            }
        }
        if (!sFavBar)
        {
            sNavHolder = root->findChildView("nav_bar_container", true);
            sFavBar    = root->findChildView("favorite", true);
            sFavBtn    = root->findChildView("landmarks_btn", true);
            if (!sFavBar)
            {
                static const char* alt[] = {"favorites_bar", "favorites", "favorite_bar", "favoritesbar"};
                for (int ai = 0; ai < 4 && !sFavBar; ++ai)
                {
                    sFavBar = root->findChildView(alt[ai], true);
                }
            }
            {
                static S32 sFavLogCount = 0;
                if (sFavLogCount < 3)
                {
                    ++sFavLogCount;
                    LL_INFOS("HyenaDebug") << "HYENA: favorites search: holder=" << (sNavHolder ? sNavHolder->getName() : std::string("none"))
                        << " favbar=" << (sFavBar ? sFavBar->getName() : std::string("none"))
                        << " navbar exists=" << (int)LLNavigationBar::instanceExists() << LL_ENDL;
                }
            }
            if (!sNavHolder && sFavBar)
            {
                // walk up from the favorites bar to the container that holds the navigation bar
                std::string chain;
                for (LLView* v = sFavBar->getParent(); v && v != root; v = v->getParent())
                {
                    chain += v->getName() + " > ";
                    if (!sNavHolder && (v->getName().find("nav_bar") != std::string::npos ||
                        v->getName().find("navigation_bar") != std::string::npos))
                    {
                        sNavHolder = v;
                    }
                }
                LL_INFOS("HyenaDebug") << "HYENA: favorites ancestors: " << chain << " picked="
                    << (sNavHolder ? sNavHolder->getName() : std::string("none")) << LL_ENDL;
            }
            if (!sNavHolder)
            {
                sFavBar = NULL;
            }
            if (sFavBar && sNavHolder)
            {
                // The viewer hides the whole navigation bar (and the favorites in it) when the mini-location bar is on.
                // Show just the favorites row: hide the back/forward/location part, move the row up to the window so
                // it is drawn and clickable under our bar.
                // the 2 px separator line along the top of the navigation bar, and its background/bevel pictures
                const char* const junk[] = { "separator_image", "bg_icon", "bg_icon_no_fav_bevel", "bg_icon_no_nav_bevel" };
                for (size_t ji = 0; ji < sizeof(junk) / sizeof(junk[0]); ++ji)
                {
                    if (LLView* jv = sNavHolder->findChildView(junk[ji], true))
                    {
                        jv->setVisible(false);
                    }
                }
                if (LLView* handle = sNavHolder->findChildView("resize_handle", true))
                {
                    handle->setVisible(false); // the thin vertical line at the left end of the row
                }
                LLView* navp = sNavHolder->findChildView("navigation_layout_panel", true);
                if (navp)
                {
                    navp->setVisible(false);
                }
                if (sNavHolder->getParent() != root)
                {
                    LLView* old = sNavHolder->getParent();
                    if (old)
                    {
                        old->removeChild(sNavHolder);
                    }
                    root->addChild(sNavHolder);
                }
                // behind everything else, so the menu drop-downs and other pop-up boxes open in front of the row
                root->sendChildToBack(sNavHolder);
                sNavHolder->setVisible(true);
                // the favorites panel normally refuses to be narrower than 342 px; let it shrink to the row
                if (LLLayoutPanel* flp = dynamic_cast<LLLayoutPanel*>(sFavBar->getParent()))
                {
                    flp->setMinDim(40);
                }
                // one-time note for the log: why might the favorites row not be on screen?
                LLView* fp = sFavBar->getParent();
                LLView* np = sNavHolder->getParent();
                LL_INFOS("HyenaDebug") << "HYENA: favorites row: nav_bar_container visible=" << (int)sNavHolder->getVisible()
                    << " rect=" << sNavHolder->getRect() << " | its parent=" << (np ? np->getName() : std::string("-"))
                    << " visible=" << (np ? (int)np->getVisible() : -1) << " rect=" << (np ? np->getRect() : LLRect())
                    << " | favorites bar rect=" << sFavBar->getRect() << " in "
                    << (fp ? fp->getName() : std::string("-"))
                    << " | ShowNavbarFavoritesPanel=" << (int)gSavedSettings.getBOOL("ShowNavbarFavoritesPanel")
                    << " ShowNavbarNavigationPanel=" << (int)gSavedSettings.getBOOL("ShowNavbarNavigationPanel")
                    << LL_ENDL;
            }
        }
    }

    if (sWellBtn)
    {
        static const S32      WELL_W = 46;
        static const S32      WELL_H = 38;
        static const LLColor4 NAVY(0.02f, 0.05f, 0.09f, 1.f);
        static const LLColor4 LIGHT(0.85f, 0.93f, 0.97f, 1.f);
        LLView*               well = sWellBtn->getParent();
        if (well && !well->getVisible())
        {
            well->setVisible(true); // never let the box vanish when the count is cleared
        }
        if (sWellPanel && !sWellPanel->getVisible())
        {
            sWellPanel->setVisible(true); // the viewer hides this slot when empty, and the list needs it to dock to
        }
        if (!sWellBtn->getEnabled())
        {
            sWellBtn->setEnabled(true); // stays clickable (and lights up on hover) even with nothing new
        }
        if (well && well->getParent() == root)
        {
            const LLRect rr  = root->getRect();
            const S32    top = rr.getHeight() - HYENA_BAR_HEIGHT - 1;
            const LLRect wr(rr.getWidth() - WELL_W - 3, top, rr.getWidth() - 3, top - WELL_H);
            if (well->getRect() != wr)
            {
                well->setShape(wr, true);
            }
            const LLRect br(0, WELL_H, WELL_W, 0);
            if (sWellBtn->getRect() != br)
            {
                sWellBtn->setShape(br, true);
            }
        }
        const std::string& label = sWellBtn->getLabelUnselected();
        const bool         lit   = !label.empty() || sWellBtn->getToggleState();
        if (sWellCount && sWellCount->getText() != label)
        {
            sWellCount->setText(label);
        }
        const LLRect ir(WELL_W / 2 - 13, WELL_H - 3, WELL_W / 2 + 13, WELL_H - 3 - 19);
        if (sWellIcon && sWellIcon->getRect() != ir)
        {
            sWellIcon->setRect(ir);
        }
        const LLRect cr(0, 14, WELL_W, 2);
        if (sWellCount && sWellCount->getRect() != cr)
        {
            sWellCount->setRect(cr);
        }
        if (sWellLit != (lit ? 1 : 0))
        {
            sWellLit          = lit ? 1 : 0;
            LLUIImagePtr dark = LLUI::getUIImage("Field_Round");
            LLUIImagePtr cyan = LLUI::getUIImage("Button_Cyan");
            sWellBtn->setImageUnselected(lit ? cyan : dark);
            sWellBtn->setImageSelected(cyan);
            sWellBtn->setImageHoverUnselected(cyan);
            sWellBtn->setImageHoverSelected(cyan);
            sWellBtn->setImagePressed(cyan);
            // nothing new: the box stays, as a dim see-through dark pill (the viewer disables the button then, so the
            // disabled look is set too)
            sWellBtn->setImageDisabled(dark);
            sWellBtn->setImageDisabledSelected(dark);
            const LLColor4 tint = lit ? LLColor4(1.f, 1.f, 1.f, 1.f) : LLColor4(1.f, 1.f, 1.f, 0.55f);
            sWellBtn->setImageColor(tint);
            sWellBtn->setDisabledImageColor(tint);
            if (sWellIcon)
            {
                sWellIcon->setColor(lit ? NAVY : LLColor4(0.85f, 0.93f, 0.97f, 0.6f));
            }
            if (sWellCount)
            {
                sWellCount->setColor(lit ? NAVY : LIGHT);
            }
        }
    }

    if (sFavBar && sNavHolder && (sFrame % 60) == 10 && LLNavigationBar::instanceExists())
    {
        // keep the favorites row switched on (the navigation part stays off)
        if (!gSavedSettings.getBOOL("ShowNavbarFavoritesPanel")) gSavedSettings.setBOOL("ShowNavbarFavoritesPanel", true);
        if (!gSavedSettings.getBOOL("FSInternalShowNavbarFavoritesPanel")) gSavedSettings.setBOOL("FSInternalShowNavbarFavoritesPanel", true);
        if (!sFavBar->getVisible()) sFavBar->setVisible(true);
        if (LLView* fpv = sFavBar->getParent()) { if (!fpv->getVisible()) fpv->setVisible(true); }
        // the navigation bar was painted in the menu bar's color when it was created (dark red on some grids); we draw
        // our own dark box behind the favorites, so drop the bar's own background and shadow
        // no thin outline/line around the row: the row's panels have borders of their own
        if (LLPanel* np = dynamic_cast<LLPanel*>(sNavHolder))
        {
            np->setBorderVisible(false);
            np->setBackgroundVisible(false);
        }
        if (LLPanel* fp = dynamic_cast<LLPanel*>(sFavBar->getParent()))
        {
            fp->setBorderVisible(false);
            fp->setBackgroundVisible(false);
        }
        if (sFavBtn && sFavBtn->getVisible())
        {
            sFavBtn->setVisible(false); // the globe (My Landmarks) button is not wanted
        }
        // the favorite buttons draw their own outline inside our box (a double line); make them flat
        const LLView::child_list_t* bk = sFavBar->getChildList();
        if (bk)
        {
            LLUIImagePtr flat = LLUI::getUIImage("Clear_Pixel");
            for (LLView::child_list_const_iter_t it = bk->begin(); it != bk->end(); ++it)
            {
                if (LLButton* fb = dynamic_cast<LLButton*>(*it))
                {
                    fb->setImageUnselected(flat);
                    fb->setImageSelected(flat);
                    fb->setImageDisabled(flat);
                    fb->setHAlign(LLFontGL::HCENTER); // text centered inside its button
                    fb->setLeftHPad(0);
                    fb->setRightHPad(0);
                }
            }
        }
    }
    if (sFavBar && sNavHolder && (sFrame % 60) == 30)
    {
        static std::string sLastFav;
        LLRect             fr;
        sFavBar->localRectToOtherView(sFavBar->getLocalRect(), &fr, root);
        const std::string now =
            llformat("fav in window=(%d,%d)-(%d,%d) visibleChain=%d children=%d | row in window parent=%s visible=%d", fr.mLeft, fr.mBottom,
                fr.mRight, fr.mTop, (int)sFavBar->isInVisibleChain(), (int)sFavBar->getChildCount(),
                sNavHolder->getParent() ? sNavHolder->getParent()->getName().c_str() : "-", (int)sNavHolder->getVisible());
        if (now != sLastFav)
        {
            sLastFav = now;
            LL_INFOS("HyenaDebug") << "HYENA: favorites now " << now << LL_ENDL;
        }
    }
    if (sFavBar && sNavHolder && !sNavHolder->getVisible() && gViewerWindow->getUIVisibility())
    {
        // the viewer hides the navigation bar again after login (mini-location mode); keep the favorites row up
        // (but still let "hide UI" hide it)
        sNavHolder->setVisible(true);
    }
    if (sFavBar && sNavHolder && sNavHolder->getParent() == root)
    {
        const LLRect rr = root->getRect();
        // Under the logo, running across about as far as the first two menus (Me, Avatar). Change HYENA_FAV_ROW_RIGHT to
        // make it longer or shorter.
        static const S32 HYENA_FAV_ROW_LEFT  = 6;
        static const S32 HYENA_FAV_ROW_RIGHT = 150;
        const S32        row_top             = rr.getHeight() - HYENA_BAR_HEIGHT - 1; // gap under the bar in pixels (smaller = closer)
        // slide the row left so the first favorite (not the hidden globe button before it) starts under the logo
        LLRect fav_in_row;
        sFavBar->localRectToOtherView(sFavBar->getLocalRect(), &fav_in_row, sNavHolder);
        const S32    row_left = HYENA_FAV_ROW_LEFT - fav_in_row.mLeft;
        const LLRect nrow(row_left, row_top, row_left + fav_in_row.mLeft + HYENA_FAV_ROW_RIGHT - HYENA_FAV_ROW_LEFT,
            row_top - sNavHolder->getRect().getHeight());
        if (sNavHolder->getRect() != nrow)
        {
            sNavHolder->setShape(nrow, true);
        }
    }
    if (sFavBar && sNavHolder && sFavBar->getVisible())
    {
        LLRect r;
        sFavBar->localRectToOtherView(sFavBar->getLocalRect(), &r, sNavHolder);
        S32 left = r.mLeft;
        if (sFavBtn && sFavBtn->getVisible())
        {
            LLRect lr;
            sFavBtn->localRectToOtherView(sFavBtn->getLocalRect(), &lr, sNavHolder);
            left = llmin(left, lr.mLeft);
        }
        LLIconCtrl* pill = sNavHolder->findChild<LLIconCtrl>("hyena_pill_favorites", false);
        if (!pill)
        {
            LLIconCtrl::Params ip;
            ip.name("hyena_pill_favorites");
            ip.rect(LLRect(0, 22, 10, 0));
            ip.image(LLUI::getUIImage("Field_Round").get());
            ip.mouse_opaque(false);
            ip.follows.flags(FOLLOWS_LEFT | FOLLOWS_TOP);
            pill = LLUICtrlFactory::create<LLIconCtrl>(ip);
            sNavHolder->addChildInBack(pill);
        }
        // fit the box to what is in the bar (the favorite buttons), not to the whole bar
        S32                         content_left   = 0x7fffffff;
        S32                         content_right  = 0;
        S32                         content_bottom = 0x7fffffff;
        S32                         content_top    = 0;
        const LLView::child_list_t* fkids          = sFavBar->getChildList();
        if (fkids)
        {
            for (LLView::child_list_const_iter_t it = fkids->begin(); it != fkids->end(); ++it)
            {
                LLView* fc = *it;
                if (fc && fc->getVisible())
                {
                    content_left   = llmin(content_left, fc->getRect().mLeft);
                    content_right  = llmax(content_right, fc->getRect().mRight);
                    content_bottom = llmin(content_bottom, fc->getRect().mBottom);
                    content_top    = llmax(content_top, fc->getRect().mTop);
                }
            }
        }
        const bool has_content = content_right > content_left;
        pill->setVisible(has_content);
        pill->setColor(LLColor4(1.f, 1.f, 1.f, 0.55f)); // see-through so it is not too dark
        // the box starts at the landmarks (globe) button, which sits just left of the bar, and ends after the last favorite
        S32 box_top    = r.mTop;
        S32 box_bottom = r.mBottom;
        if (has_content && content_top > content_bottom)
        {
            // same space above and below the text
            box_top    = r.mBottom + content_top + 3;
            box_bottom = r.mBottom + content_bottom - 3;
        }
        S32 box_left = has_content ? r.mLeft + content_left - 5 : left - 2;
        if (has_content && sFavBtn && sFavBtn->getVisible())
        {
            LLRect lr2;
            sFavBtn->localRectToOtherView(sFavBtn->getLocalRect(), &lr2, sNavHolder);
            box_left   = llmin(box_left, lr2.mLeft - 2);
            box_top    = llmax(box_top, lr2.mTop);
            box_bottom = llmin(box_bottom, lr2.mBottom);
        }
        const S32    box_right = has_content ? r.mLeft + content_right + 5 : r.mRight + 2;
        const LLRect want(box_left, box_top, box_right, box_bottom);
        if (pill->getRect() != want)
        {
            pill->setRect(want);
        }
    }
}

// Status-bar pop-ups (camera presets, graphics presets, volume, media). The viewer lines them up with the right edge of
// the WINDOW, which assumes the status bar runs to the window edge; ours stops before the window buttons. So: theme them
// once (even while hidden, so nothing flashes in the old style), and put each one centered under its own icon the
// moment it opens, then leave it there until it closes.
static void hyena_place_popups(S32 cw)
{
    static std::map<LLView*, LLRect> sPopupSpot;
    static std::map<LLView*, bool>   sThemed;
    if (!gStatusBar)
    {
        return;
    }
    const LLView::child_list_t* kids = gStatusBar->getChildList();
    if (!kids)
    {
        return;
    }
    for (LLView::child_list_const_iter_t it = kids->begin(); it != kids->end(); ++it)
    {
        LLView* c = *it;
        if (!c)
        {
            continue;
        }
        const char* icon_name = NULL;
        if (dynamic_cast<LLPanelPresetsCameraPulldown*>(c))
        {
            icon_name = "presets_icon_camera";
        }
        else if (dynamic_cast<LLPanelPresetsPulldown*>(c))
        {
            icon_name = "presets_icon_graphic";
        }
        else if (dynamic_cast<LLPanelVolumePulldown*>(c))
        {
            icon_name = "volume_btn";
        }
        else if (dynamic_cast<LLPanelNearByMedia*>(c))
        {
            icon_name = "media_toggle_btn";
        }
        else
        {
            continue;
        }
        if (!sThemed[c])
        {
            sThemed[c] = true;
            if (LLPanel* pp = dynamic_cast<LLPanel*>(c))
            {
                // the pop-up XML paints a texture ("Volume_Background") over the color, so drop the textures
                pp->setBackgroundImage(NULL);
                pp->setTransparentImage(NULL);
                pp->setBackgroundVisible(true);
                pp->setBackgroundOpaque(true);
                pp->setBackgroundColor(LLColor4(0.047f, 0.078f, 0.114f, 1.f));
            }
            hyena_theme_popup(c);
        }
        if (!c->getVisible())
        {
            sPopupSpot.erase(c);
            continue;
        }
        LLView* icon = gStatusBar->findChildView(icon_name, true);
        if (!icon)
        {
            continue;
        }
        const LLRect                        pr = c->getRect();
        const S32                           pw = pr.getWidth();
        const S32                           ph = pr.getHeight();
        LLRect                              want;
        std::map<LLView*, LLRect>::iterator spot = sPopupSpot.find(c);
        if (spot == sPopupSpot.end())
        {
            LLRect ir = icon->getRect();
            for (LLView* par = icon->getParent(); par && par != gStatusBar; par = par->getParent())
            {
                ir.translate(par->getRect().mLeft, par->getRect().mBottom); // the icons sit inside a panel of the bar
            }
            S32          right = ir.getCenterX() + pw / 2; // centred under the icon
            right              = llmin(right, cw - 4);     // ...but kept on the screen
            const S32 left     = llmax(right - pw, 0);
            const S32 top      = ir.mBottom - (HYENA_BAR_HEIGHT - HYENA_STATUS_HEIGHT) / 2; // just under the bar
            want               = LLRect(left, top, left + pw, top - ph);
            sPopupSpot[c]      = want;
        }
        else
        {
            // keep the spot where it first opened, but follow the pop-up's CURRENT size (Nearby Media grows
            // when "More" is pressed, and its background has to grow with it)
            want = LLRect(spot->second.mLeft, spot->second.mTop, spot->second.mLeft + pw, spot->second.mTop - ph);
        }
        if (pr != want)
        {
            c->setShape(want);
        }
    }
}

static const char* const HYENA_BAR_WIDGETS[] = { "titlebar_logo_box", "titlebar_logo", "titlebar_title", "build_label",
"win_min_btn",       "win_max_btn",   "win_close_btn",  "titlebar_line" };

struct HyenaBarWidget
{
    LLView* view;
    S32     top_off;   // distance from the top of whatever holds it
    S32     right_off; // distance from the right edge
    S32     left;
    S32     width;
    S32     height;
};
static std::vector<HyenaBarWidget> sBarWidgets;
static LLView*                     sBarParent = NULL;

// Moves the bar parts into `target` (the launcher on the login screen, status_bar_container in-world)
static void move_bar_widgets(LLPanel* launcher, LLView* target)
{
    if (!launcher || !target)
    {
        return;
    }
    if (sBarWidgets.empty())
    {
        const LLRect prect = launcher->getRect();
        for (size_t i = 0; i < sizeof(HYENA_BAR_WIDGETS) / sizeof(HYENA_BAR_WIDGETS[0]); ++i)
        {
            LLView* v = launcher->findChildView(HYENA_BAR_WIDGETS[i], true);
            if (!v)
            {
                continue;
            }
            const LLRect   r = v->getRect();
            HyenaBarWidget b;
            b.view      = v;
            b.top_off   = prect.getHeight() - r.mTop;
            b.right_off = prect.getWidth() - r.mRight;
            b.left      = r.mLeft;
            b.width     = r.getWidth();
            b.height    = r.getHeight();
            sBarWidgets.push_back(b);
        }
        sBarParent = launcher;
    }
    if (sBarParent == target)
    {
        return;
    }
    const S32 tw = target->getRect().getWidth();
    const S32 th = target->getRect().getHeight();
    for (size_t i = 0; i < sBarWidgets.size(); ++i)
    {
        HyenaBarWidget& b = sBarWidgets[i];
        if (b.view->getParent())
        {
            b.view->getParent()->removeChild(b.view);
        }
        target->addChild(b.view);
        const U32 f = b.view->getFollows();
        S32       l = b.left;
        S32       r = b.left + b.width;
        if (f & FOLLOWS_RIGHT)
        {
            r = tw - b.right_off;
            if (!(f & FOLLOWS_LEFT))
            {
                l = r - b.width;
            }
        }
        b.view->setShape(LLRect(l, th - b.top_off, r, th - b.top_off - b.height), true);
    }
    sBarParent = target;
}

// Empty parts of the in-world bar drag the window (double-click maximizes), like a normal title bar.
// The middle of the bar belongs to the viewer so its buttons work, so this is done from inside the viewer.
class HyenaDragStrip : public LLView
{
public:
    HyenaDragStrip(const LLView::Params& p) : LLView(p) {}
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        HWND hwnd = gViewerWindow ? (HWND)gViewerWindow->getPlatformWindow() : NULL;
        if (!hwnd)
        {
            return false;
        }
        POINT pt;
        GetCursorPos(&pt);
        PostMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
        return true;
    }
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override
    {
        LLWindow* window = gViewerWindow->getWindow();
        if (window->getMaximized())
        {
            window->restore();
        }
        else
        {
            window->maximize();
        }
        return true;
    }
};

// A rounded "pill" behind a group of status items (the same rounded field look as the launcher).
static void place_pill(LLView* container, const char* name, S32 left, S32 right, S32 bar_top, bool show)
{
    static const S32 PILL_HEIGHT = 22;
    LLIconCtrl*      pill        = container->findChild<LLIconCtrl>(name);
    if (!pill)
    {
        LLIconCtrl::Params ip;
        ip.name(name);
        ip.rect(LLRect(0, PILL_HEIGHT, 10, 0));
        ip.image(LLUI::getUIImage("Field_Round").get());
        ip.mouse_opaque(false);
        ip.follows.flags(FOLLOWS_LEFT | FOLLOWS_TOP);
        pill = LLUICtrlFactory::create<LLIconCtrl>(ip);
        container->addChildInBack(pill);
    }
    LLRect r;
    r.setOriginAndSize(left, bar_top - (HYENA_BAR_HEIGHT - PILL_HEIGHT) / 2 - PILL_HEIGHT, llmax(right - left, 1), PILL_HEIGHT);
    if (pill->getRect() != r)
    {
        pill->setRect(r);
    }
    pill->setVisible(show);
}

// Rectangle of a status-bar item, in the coordinates of the strip that holds the bar
static bool status_item_rect(LLView* bar, const char* name, LLRect& out)
{
    LLView* v = bar->findChildView(name, true);
    if (!v || !v->getVisible() || v->getRect().getWidth() <= 1)
    {
        return false;
    }
    out = v->getRect();
    for (LLView* p = v->getParent(); p && p != bar; p = p->getParent())
    {
        out.translate(p->getRect().mLeft, p->getRect().mBottom); // items can sit inside a panel of the bar
    }
    out.translate(bar->getRect().mLeft, bar->getRect().mBottom);
    return true;
}

static void apply_custom_frame(LLPanel* launcher)
{
    if (!gViewerWindow || !launcher)
    {
        return;
    }
    LLWindowWin32* win = dynamic_cast<LLWindowWin32*>(gViewerWindow->getWindow());
    if (!win)
    {
        return;
    }
    const bool     login_visible = launcher->isInVisibleChain();
    const F32      scale         = gViewerWindow->getDisplayScale().mV[VX];
    const LLColor4 bar_color(0.047f, 0.078f, 0.114f, 1.f);

    LLView* root = gViewerWindow->getRootView();
    // Searching the whole window by name every frame walks the entire interface, including the inventory
    // (about 200 ms per frame). Find each view once, remember it, and only retry (once a second) if it is missing.
    static LLView* sContainer   = NULL;
    static LLView* sHolder      = NULL;
    static LLView* sNav         = NULL;
    static S32     sLookupFrame = 0;
    if (root && (!sContainer || !sHolder || !sNav) && (sLookupFrame++ % 60) == 0)
    {
        if (!sContainer) { sContainer = root->findChildView("status_bar_container", true); }
        if (!sHolder)    { sHolder    = root->findChildView("menu_bar_holder", true); }
        if (!sNav)       { sNav       = root->findChildView("nav_bar_container", true); }
    }
    LLView* container = sContainer;
    LLView* holder    = sHolder;
    LLView* nav       = sNav;

    // The strip is made taller ONCE (19 -> 34 px) and the navigation bar is pushed down by the same amount.
    // The viewer itself adds / removes the navigation bar height with relative changes, so we must never set the
    // strip height to a fixed number again (doing that is what broke the toolbars and coordinates in-world).
    static bool sExtraApplied = false;
    if (container && holder)
    {
        const S32 extra = HYENA_BAR_HEIGHT - HYENA_STRIP_HEIGHT;
        if (!sExtraApplied)
        {
            sExtraApplied = true;
            container->reshape(container->getRect().getWidth(), container->getRect().getHeight() + extra, true);
            if (nav)
            {
                nav->translate(0, -extra);
            }
        }
        if (holder->getRect().getHeight() != HYENA_BAR_HEIGHT)
        {
            holder->reshape(holder->getRect().getWidth(), HYENA_BAR_HEIGHT, true);
        }
        // Login: clicks must reach the launcher below, so the strip lets them through and has no background.
        // In-world: the strip owns the bar (our buttons are inside it) and paints the bar colour.
        container->setMouseOpaque(!login_visible);
        LLPanel* container_panel = dynamic_cast<LLPanel*>(container);
        if (container_panel)
        {
            container_panel->setBackgroundVisible(!login_visible);
            container_panel->setBackgroundOpaque(true);
            container_panel->setBackgroundColor(bar_color);
        }
        move_bar_widgets(launcher, login_visible ? (LLView*)launcher : container);
        // Login screen: logo + "PROJECT HYENA". In-world: the logo alone in a small box.
        for (size_t i = 0; i < sBarWidgets.size(); ++i)
        {
            LLView*           v    = sBarWidgets[i].view;
            const std::string name = v->getName();
            if (name == "titlebar_title")
            {
                v->setVisible(login_visible);
            }
            else if (name == "titlebar_logo")
            {
                // login: the logo sits at the left of the box; in-world it is centred in the small box
                const LLRect r    = v->getRect();
                const S32    left = login_visible ? 13 : 4 + (HYENA_BOX_WIDTH_WORLD - r.getWidth()) / 2;
                if (r.mLeft != left)
                {
                    v->setShape(LLRect(left, r.mTop, left + r.getWidth(), r.mBottom), true);
                }
            }
            else if (name == "titlebar_logo_box")
            {
                const LLRect r  = v->getRect();
                const S32    w  = login_visible ? HYENA_BOX_WIDTH_LOGIN : HYENA_BOX_WIDTH_WORLD;
                const S32    bl = login_visible ? 8 : 4; // in-world the box sits closer to the window edge
                if (r.getWidth() != w || r.mLeft != bl)
                {
                    v->setShape(LLRect(bl, r.mTop, bl + w, r.mBottom), true);
                }
            }
        }
        // The build label would sit on top of the status items in-world, so it is only shown on the login screen
        for (size_t i = 0; i < sBarWidgets.size(); ++i)
        {
            if (sBarWidgets[i].view->getName() == "build_label")
            {
                sBarWidgets[i].view->setVisible(login_visible);
                // the text comes from the version defined once in llappviewer.cpp
                if (LLTextBox* bl = dynamic_cast<LLTextBox*>(sBarWidgets[i].view))
                {
                    const std::string want = HYENA_STAGE + " " + HYENA_VERSION + " (" + HYENA_BUILD_TAG + ")";
                    if (bl->getText() != want)
                    {
                        bl->setText(want);
                    }
                }
            }
        }
    }

    // Slide the menu next to the logo, centred in the bar. In-world that is the viewer's menu, on the login
    // screen it is the login menu. Worked out in window coordinates, then converted.
    LLMenuBarGL* menu       = (!login_visible && gMenuBarView && gMenuBarView->getVisible()) ? gMenuBarView : gLoginMenuBarView;
    const S32    menu_left  = login_visible ? HYENA_MENU_LEFT_LOGIN : HYENA_MENU_LEFT_WORLD;
    S32          menu_right = menu_left;
    if (menu && menu->getParent() && root)
    {
        static S32 sLoginMenuHeight = 0;
        static S32 sWorldMenuHeight = 0;
        S32&       remembered       = (menu == gMenuBarView) ? sWorldMenuHeight : sLoginMenuHeight;
        if (remembered <= 0)
        {
            remembered = menu->getRect().getHeight();
        }
        // Tighter spacing between the menu names: each one gets a few pixels narrower than the viewer makes it
        static const S32 MENU_TIGHTEN = 12;
        S32              packed       = 0;
        for (S32 i = 0; !login_visible && i < (S32)menu->getItemCount(); ++i)
        {
            LLMenuItemGL* item = menu->getItem(i);
            if (!item || !item->getVisible())
            {
                continue;
            }
            const LLRect ir = item->getRect();
            const S32    iw = llmax((S32)item->getNominalWidth() - MENU_TIGHTEN, 10);
            if (ir.mLeft != packed || ir.getWidth() != iw)
            {
                item->setRect(LLRect(packed, ir.mTop, packed + iw, ir.mBottom));
            }
            packed += iw;
        }
        if (!login_visible && packed > 0 && menu->getRect().getWidth() != packed)
        {
            menu->reshape(packed, menu->getRect().getHeight());
        }
        const LLRect old_rect = menu->getRect();
        const S32    w        = old_rect.getWidth();
        const S32    h        = remembered;
        const S32    win_top  = root->getRect().getHeight();
        const S32    top      = win_top - (HYENA_BAR_HEIGHT - h) / 2;
        LLRect       wanted;
        wanted.set(menu_left, top, menu_left + w, top - h);
        menu_right   = menu_left + w;
        S32 parent_x = 0, parent_y = 0;
        menu->getParent()->localPointToOtherView(0, 0, &parent_x, &parent_y, root);
        LLRect local = wanted;
        local.translate(-parent_x, -parent_y);
        if (local != old_rect)
        {
            menu->setRect(local);
        }
        menu->setBackgroundColor(bar_color);
    }

    // In-world: the status items (parcel info, volume, clock ...) sit between the menu and our buttons,
    // vertically centred in the bar, and the whole middle part of the bar belongs to the viewer (clicks work).
    const S32 menu_end_ui = menu_right; // where the last menu ends, in UI units
    if (login_visible)
    {
        hyena_reset_ui_touches();
    }
    if (!login_visible && gStatusBar && container)
    {
        const S32 cw          = container->getRect().getWidth();
        const S32 ch          = container->getRect().getHeight();
        const S32 y           = ch - (HYENA_BAR_HEIGHT - HYENA_STATUS_HEIGHT) / 2;
        const S32 right_limit = cw - HYENA_BUTTONS_WIDTH; // the window buttons start here
        gStatusBar->setBackgroundVisible(false);          // the bar colour comes from the strip, pills sit on top
        gStatusBar->setBackgroundColor(bar_color);

        LLButton*      buy          = gStatusBar->findChild<LLButton>("buyL");
        LLButton*      shop         = gStatusBar->findChild<LLButton>("goShop");
        LLView*        bal_bg       = gStatusBar->findChildView("balance_bg", true);
        LLTextBox*     bal_text     = gStatusBar->findChild<LLTextBox>("balance");
        LLView*        search       = gStatusBar->findChildView("menu_search_panel", true);
        static LLView* sSearchPanel = NULL; // the search box is moved out of the bar (it opens under the magnifier)
        if (!sSearchPanel)
        {
            sSearchPanel = search;
        }
        search = sSearchPanel;

        // Buy L$ becomes a cyan button; Shop is taken out of the bar to save room (done once)
        static bool sButtonsStyled = false;
        if (!sButtonsStyled && buy)
        {
            LLUIImagePtr cyan = LLUI::getUIImage("Button_Cyan");
            buy->setImageUnselected(cyan);
            buy->setImageSelected(cyan);
            buy->setImageHoverUnselected(cyan);
            buy->setImageHoverSelected(cyan);
            buy->setImagePressed(cyan);
            buy->setUnselectedLabelColor(LLColor4(0.f, 0.05f, 0.08f, 1.f));
            buy->setSelectedLabelColor(LLColor4(0.f, 0.05f, 0.08f, 1.f));
            if (shop)
            {
                shop->setVisible(false);
                shop->reshape(1, shop->getRect().getHeight(), true);
            }
            buy->setLabel(std::string("Buy L$"));
            buy->setFont(LLFontGL::getFontSansSerif());
            if (bal_text)
            {
                bal_text->setFont(LLFontGL::getFontSansSerif());
            }
            {
                // keep the right edge, make the button narrower (80 -> 58)
                LLRect bb = buy->getRect();
                bb.mLeft  = bb.mRight - 58;
                buy->setShape(bb);
            }
            sButtonsStyled = true;
        }

        // Lay the status items out right to left with small, even gaps (the viewer's own layout leaves wide gaps and
        // an empty slot where the Shop button was). Everything is measured from the right edge of the status bar.
        {
            LLTextBox* clock = gStatusBar->findChild<LLTextBox>("TimeText");
            const S32  sbw   = gStatusBar->getRect().getWidth();
            S32        x     = sbw - 10;
            // The two little bars at the right end (packet loss % and bandwidth in Kbps) get their own pill, and
            // the other items sit to the left of them instead of on top.
            LLView* net_bw = gStatusBar->findChildView("BandwidthGraph", true);
            LLView* net_pl = gStatusBar->findChildView("PacketLossPercent", true);
            const bool net_on = net_bw && net_pl && net_bw->getVisible() && net_pl->getVisible();
            if (net_on)
            {
                S32 nl = net_pl->getRect().mLeft;
                place_pill(container, "hyena_pill_net", nl - 6, sbw + 4, ch, true);
                x = nl - 14;
            }
            else
            {
                place_pill(container, "hyena_pill_net", 0, 1, ch, false);
            }
            // The clock, preset icons, music / media / volume buttons all live inside the "time_and_media_bg" panel, so
            // they are laid out in that panel's own coordinates and the panel itself is sized to fit and parked at the
            // right edge. The viewer's own FPS number in there is hidden (the bar draws its own).
            LLView* tm = gStatusBar->findChildView("time_and_media_bg", true);
            if (LLView* old_fps = gStatusBar->findChildView("FPSText", true))
            {
                if (old_fps->getVisible())
                {
                    old_fps->setVisible(false);
                }
            }
            if (tm)
            {
                if (LLPanel* tp = dynamic_cast<LLPanel*>(tm))
                {
                    tp->setBackgroundVisible(false);
                }
                // The small icons get the Hyena colours: bright when usable, a clear slate blue when the viewer has
                // switched them off (no parcel music / media here) instead of the dull grey "deselected" look.
                static bool sIconsColoured = false;
                if (!sIconsColoured)
                {
                    const char* const btns[] = { "stream_toggle_btn", "media_toggle_btn", "volume_btn",
                        "presets_icon_camera", "presets_icon_graphic" };
                    S32               done   = 0;
                    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i)
                    {
                        if (LLButton* b = tm->findChild<LLButton>(btns[i], false))
                        {
                            b->setImageColor(LLColor4(0.85f, 0.96f, 1.f, 1.f));
                            b->setDisabledImageColor(LLColor4(0.42f, 0.58f, 0.68f, 1.f));
                            ++done;
                        }
                    }
                    sIconsColoured = (done == 5);
                }
                const char* const order[] = { "TimeText", "presets_icon_camera", "presets_icon_graphic",
                    "stream_toggle_btn", "media_toggle_btn", "volume_btn" };
                S32               cx      = 6;
                S32               gap     = 0;
                for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
                {
                    LLView* v = tm->findChildView(order[i], false);
                    if (!v || !v->getVisible())
                    {
                        continue;
                    }
                    const S32 w = (i == 0) ? (clock ? clock->getTextBoundingRect().getWidth() + 4 : v->getRect().getWidth())
                        : v->getRect().getWidth();
                    LLRect    r = v->getRect();
                    r.mLeft     = cx;
                    r.mRight    = cx + w;
                    if (v->getRect() != r)
                    {
                        v->setRect(r);
                    }
                    gap = (i == 0) ? 12 : 4;
                    cx += w + gap;
                }
                const S32 total = cx - gap + 6;
                LLRect    pr    = tm->getRect();
                pr.mRight       = x;
                pr.mLeft        = x - total;
                if (tm->getRect() != pr)
                {
                    tm->setRect(pr);
                }
                x = pr.mLeft;
            }
            x -= 6; // gap between the clock and the L$ group
            if (bal_text)
            {
                // While the viewer refreshes the balance it shows "L$ ?"; keep showing the last known amount instead.
                static std::string sLastBalance;
                const std::string  bt = bal_text->getText();
                if (bt.find_first_of("0123456789") != std::string::npos)
                {
                    sLastBalance = bt;
                }
                else if (!sLastBalance.empty() && bt.find('?') != std::string::npos)
                {
                    bal_text->setText(sLastBalance);
                }
            }
            if (bal_bg && bal_text && buy)
            {
                // inside the group: [ L$ amount ] [ Buy L$ ] (Shop is out of the bar)
                const S32 tw    = bal_text->getTextBoundingRect().getWidth() + 2;
                const S32 buy_w = 58;
                const S32 total = 6 + tw + 6 + buy_w + 6;
                LLRect    br    = bal_bg->getRect();
                br.mRight       = x;
                br.mLeft        = x - total;
                if (bal_bg->getRect() != br)
                {
                    bal_bg->setShape(br);
                }
                const S32 hh = bal_bg->getRect().getHeight();
                LLRect    tr(6, hh, 6 + tw, 0);
                if (bal_text->getRect() != tr)
                {
                    bal_text->setRect(tr);
                }
                LLRect yr(6 + tw + 6, hh, 6 + tw + 6 + buy_w, 0);
                if (buy->getRect() != yr)
                {
                    buy->setRect(yr);
                }
                LLRect sr2(total, hh, total + 1, 0);
                if (shop && shop->getRect() != sr2)
                {
                    shop->setRect(sr2);
                }
            }
        }

        // ---- the location strip (parcel name, region, coordinates, rating, parcel icons) ----
        S32                loc_start         = menu_end_ui + 4;
        S32                loc_width         = 0;
        S32                loc_y             = 0;
        S32                loc_h             = HYENA_STATUS_HEIGHT;
        bool               have_loc          = false;
        S32                loc_width_compact = 0; // width when the parcel name is left out
        S32                compact_shift     = 0; // how far the strip is slid left in that case
        S32                compact_text_w    = 0;
        std::string        compact_str;
        std::string        full_str;
        S32                full_text_w   = 0;
        S32                loc_text_x    = 12; // where our location text starts (after the parcel icons)
        LLTextBox*         loc_info_text = NULL;
        static LLPanel*    sInfoHolder   = NULL;
        static LLTextBox*  sLocText      = NULL;
        // Firestorm has no LLPanelTopInfoBar: its status bar holds the same thing as "parcel_info_panel" (the parcel
        // icons and the location text). It is found once, because it is moved out of the status bar into our holder.
        static LLPanel* sParcelPanel = NULL;
        if (!sParcelPanel && gStatusBar)
        {
            sParcelPanel = gStatusBar->findChild<LLPanel>("parcel_info_panel");
        }
        LLPanel* info = sParcelPanel;
        static bool sCoordsForced = false; // the location always shows the coordinates
        if (!sCoordsForced)
        {
            gSavedSettings.setBOOL("NavBarShowCoordinates", true);
            sCoordsForced = true;
        }
        if (info)
        {
            // The strip works out its own width from x = 0, so it must keep sitting at x = 0 inside a holder.
            if (!sInfoHolder)
            {
                LLPanel::Params hp;
                hp.name("hyena_info_holder");
                hp.rect(LLRect(0, 19, 300, 0));
                hp.follows.flags(FOLLOWS_LEFT | FOLLOWS_TOP);
                hp.mouse_opaque(false);
                hp.background_visible(false);
                sInfoHolder = LLUICtrlFactory::create<LLPanel>(hp);
                container->addChild(sInfoHolder);
            }
            if (info->getParent() != sInfoHolder)
            {
                if (info->getParent())
                {
                    info->getParent()->removeChild(info);
                }
                sInfoHolder->addChild(info);
                info->setFollows(FOLLOWS_LEFT | FOLLOWS_TOP);
                info->setVisible(true);
            }
            info->setBackgroundVisible(false); // the pill behind it replaces the square background
            loc_h = info->getRect().getHeight();
            if (info->getRect().mBottom != 0)
            {
                info->setOrigin(info->getRect().mLeft, 0);
            }
            // Width of what is really there: the text plus the parcel icons that are showing
            S32         content       = 12;
            LLView*     text          = info->findChildView("parcel_info_text", true);
            static bool sInfoFontSet  = false;
            LLTextBox*  info_text     = dynamic_cast<LLTextBox*>(text);
            static bool sInfoClickSet = false;
            if (!sInfoClickSet && info_text)
            {
                // clicking the region name / coordinates opens About Land
                info_text->setMouseOpaque(true);
                info_text->setToolTip(std::string("About land"));
                info_text->setClickedCallback([](void*) { open_about_land(); });
                sInfoClickSet = true;
            }
            if (!sInfoFontSet && info_text)
            {
                // a smaller, lighter font so the location takes less room; then let the strip measure itself again
                info_text->setFont(hyena_loc_font());
                info_text->setColor(LLColor4(0.90f, 0.94f, 0.97f, 1.f));
                // info->handleLoginComplete(); // not available in Firestorm
                sInfoFontSet = true;
            }
            const char* const parts[] = { "voice_icon",   "fly_icon",    "push_icon",   "build_icon",
                "scripts_icon", "damage_icon", "damage_text", "see_avatars_icon",
                "see_avs_icon", "pathfinding_dirty_icon", "pathfinding_disabled_icon", "scriptout" };
            loc_y                     = ch - (HYENA_BAR_HEIGHT - loc_h) / 2 - loc_h;
            have_loc                  = true;

            // Layout of the location: [parcel icons] [Region (x, y, z) - Rating]. The viewer lays its icons out to the
            // RIGHT of its own text, so the strip is slid left until its first icon sits at x = 12, the viewer's own
            // text is hidden and our text is drawn after the icons.
            loc_info_text = info_text;
            if (info_text)
            {
                S32 icons_left = 100000, icons_right = -100000;
                for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
                {
                    LLView* v = info->findChildView(parts[i], true);
                    if (v && v->getVisible())
                    {
                        icons_left  = llmin(icons_left, v->getRect().mLeft);
                        icons_right = llmax(icons_right, v->getRect().mRight);
                    }
                }
                const bool have_icons = icons_right > icons_left;
                compact_shift         = have_icons ? icons_left - 12 : 0;
                loc_text_x            = have_icons ? 12 + (icons_right - icons_left) + 4 : 12;

                full_str = info_text->getText();
                {
                    std::string full;
                    const bool  coords = gSavedSettings.getBOOL("NavBarShowCoordinates");
                    if (LLAgentUI::buildLocationString(full,
                        coords ? LLAgentUI::LOCATION_FORMAT_FULL : LLAgentUI::LOCATION_FORMAT_NO_COORDS))
                    {
                        size_t limit = full.rfind(" (");
                        if (limit == std::string::npos)
                        {
                            limit = full.rfind(" - ");
                        }
                        if (limit == std::string::npos)
                        {
                            limit = full.size();
                        }
                        const size_t comma = full.rfind(", ", limit);
                        compact_str        = (comma == std::string::npos) ? full : full.substr(comma + 2);
                    }
                    else
                    {
                        compact_str = full_str;
                    }
                }
                full_text_w       = info_text->getFont()->getWidth(full_str);
                compact_text_w    = info_text->getFont()->getWidth(compact_str);
                loc_width         = loc_text_x + full_text_w + 8;
                loc_width_compact = loc_text_x + compact_text_w + 8;
            }
            else
            {
                loc_width = 40;
            }
        }

        // ---- is there room for everything? Things step aside in this order: the two preset icons (camera and
        //      graphics), then the search button, then the FPS counter. They all come back when the window is wider. ----
        const S32  sb_w           = gStatusBar->getRect().getWidth();
        const S32  dist_bal       = bal_bg ? sb_w - bal_bg->getRect().mLeft : 400;
        const S32  need_full      = loc_start + (have_loc ? loc_width : 0) + 4;
        const S32  need_compact   = loc_start + (have_loc && loc_width_compact > 0 ? loc_width_compact : loc_width) + 4;
        const S32  right_fps      = right_limit - HYENA_FPS_WIDTH;
        const S32  BTN_W          = 18;
        const S32  BTN_GAP        = 3;
        const bool search_allowed = search && gSavedSettings.getBOOL("MenuSearch");

        LLView* preset_cam = gStatusBar->findChildView("presets_icon_camera", true);
        LLView* preset_gfx = gStatusBar->findChildView("presets_icon_graphic", true);
        S32     presets_w  = 0;
        S32     cam_w      = 0;
        if (preset_cam)
        {
            cam_w = preset_cam->getRect().getWidth() + 4;
            presets_w += cam_w;
        }
        if (preset_gfx)
        {
            presets_w += preset_gfx->getRect().getWidth() + 4;
        }
        // distance from the L$ group to the right edge with both preset icons showing
        const S32 dist_full = dist_bal + ((preset_cam && !preset_cam->getVisible()) ? cam_w : 0) +
            ((preset_gfx && !preset_gfx->getVisible()) ? presets_w - cam_w : 0);

        bool      presets_on = true;
        bool      cam_on     = true;
        bool      fps_on     = true;
        bool      btn_on     = search_allowed;
        bool      compact    = false;
        const S32 btn_space  = BTN_W + BTN_GAP;
        const S32 btn_cost   = btn_on ? btn_space : 0;
        if (need_full <= right_fps - dist_full - btn_cost)
        {
            // everything fits
        }
        else
        {
            compact = loc_width_compact > 0; // leave the parcel name out of the location
            if (loc_start + 60 <= right_fps - dist_full - btn_cost)
            {
                // fits with the short location
            }
            else
            {
                cam_on = false; // first the camera preset icon goes; the graphics (monitor) icon always stays
                const S32 dist_gfx = dist_full - cam_w;
                if (loc_start + 60 <= right_fps - dist_gfx - btn_cost)
                {
                    // fits with only the graphics preset icon
                }
                else
                {
                    // the search magnifier always stays; the FPS counter goes instead
                    fps_on = false;
                }
            }
        }
        if (preset_cam)
        {
            preset_cam->setVisible(presets_on && cam_on);
        }
        if (preset_gfx)
        {
            preset_gfx->setVisible(presets_on);
        }

        const S32 sb_right = fps_on ? right_fps : right_limit;
        LLRect    sb_rect(0, y, sb_right, y - HYENA_STATUS_HEIGHT);
        if (gStatusBar->getRect() != sb_rect)
        {
            gStatusBar->setShape(sb_rect, true);
        }

        // Clicking an icon opens its pop-up too (the viewer only opens them on hover)
        {
            static bool sClickHooked = false;
            if (!sClickHooked)
            {
                const char* const names[] = { "presets_icon_camera", "presets_icon_graphic", "volume_btn", "media_toggle_btn" };
                S32               hooked  = 0;
                for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
                {
                    LLUICtrl* ic = gStatusBar->findChild<LLUICtrl>(names[i], true);
                    if (ic)
                    {
                        ic->setMouseDownCallback([](LLUICtrl* c, S32 x, S32 y, MASK mask) { c->onMouseEnter(x, y, mask); });
                        // runs right after the viewer's own enter handler, so the pop-up is moved before it is drawn
                        ic->setMouseEnterCallback(
                            [](LLUICtrl*, const LLSD&)
                            {
                                if (gStatusBar && gStatusBar->getParent())
                                {
                                    hyena_place_popups(gStatusBar->getParent()->getRect().getWidth());
                                }
                            });
                        ++hooked;
                    }
                }
                sClickHooked = (hooked == 4);
            }
        }

        // Pop-ups: placed under their icons, themed, and kept still (see hyena_place_popups)
        hyena_place_popups(cw);
        hyena_ui_touches(root);

        // ---- menu search: a magnifier button; clicking it opens the search box next to it ----
        {
            static LLButton* sSearchBtn = NULL;
            if (!sSearchBtn)
            {
                LLUIImagePtr     icon = LLUI::getUIImage("Command_Search_Icon");
                LLButton::Params bp;
                bp.name("hyena_search_btn");
                bp.rect(LLRect(0, 22, 22, 0));
                bp.follows.flags(FOLLOWS_LEFT | FOLLOWS_TOP);
                bp.label(std::string());
                bp.tool_tip(std::string("Search menus"));
                bp.image_unselected(icon.get());
                bp.image_selected(icon.get());
                bp.image_hover_unselected(icon.get());
                bp.image_hover_selected(icon.get());
                bp.image_pressed(icon.get());
                bp.image_pressed_selected(icon.get());
                bp.image_color(LLColor4(0.f, 0.898f, 1.f, 1.f));
                sSearchBtn = LLUICtrlFactory::create<LLButton>(bp);
                sSearchBtn->setCommitCallback(
                    [](LLUICtrl*, const LLSD&)
                    {
                        sSearchOpen = !sSearchOpen;
                        if (sSearchOpen)
                        {
                            sSearchJustOpened = true;
                            sSearchOpenTimer.reset();
                        }
                    });
                container->addChild(sSearchBtn);
            }

            const S32 bal_left = sb_right - dist_bal; // where the L$ group starts
            const S32 btn_left = bal_left - BTN_GAP - BTN_W;
            LLRect    brect;
            brect.setOriginAndSize(btn_left + (BTN_W - 16) / 2, ch - (HYENA_BAR_HEIGHT - 16) / 2 - 16, 16, 16);
            if (sSearchBtn->getRect() != brect)
            {
                sSearchBtn->setRect(brect);
            }
            sSearchBtn->setVisible(btn_on);
            place_pill(container, "hyena_pill_search", btn_left, btn_left + BTN_W, ch, btn_on);

            if (search)
            {
                LLView* root_view = gViewerWindow->getRootView();
                if (root_view && search->getParent() != root_view)
                {
                    if (search->getParent())
                    {
                        search->getParent()->removeChild(search);
                    }
                    root_view->addChild(search);
                    search->setFollows(FOLLOWS_LEFT | FOLLOWS_TOP);
                }
                if (!btn_on)
                {
                    sSearchOpen = false;
                }
                LLUICtrl* edit = search->findChild<LLUICtrl>("search_menu_edit", true);
                if (sSearchOpen && edit)
                {
                    if (sSearchJustOpened)
                    {
                        edit->setFocus(true);
                        sSearchJustOpened = false;
                    }
                    else if (sSearchOpenTimer.getElapsedTimeF32() > 0.6f && !edit->hasFocus() && edit->getValue().asString().empty())
                    {
                        sSearchOpen = false; // clicked away with nothing typed: fold back into the button
                    }
                }
                if (root_view)
                {
                    // right under the magnifier, lined up with its right edge
                    S32 rx = 0, ry = 0;
                    container->localPointToOtherView(0, ch, &rx, &ry, root_view);
                    const LLRect sr = search->getRect();
                    const S32    l  = llmax(btn_left + BTN_W - sr.getWidth(), 0);
                    const S32    t  = ry - HYENA_BAR_HEIGHT - 2;
                    const LLRect want(l, t, l + sr.getWidth(), t - sr.getHeight());
                    if (sr != want)
                    {
                        search->setShape(want);
                    }
                }
                search->setVisible(sSearchOpen);
                if (LLPanel* sp = dynamic_cast<LLPanel*>(search))
                {
                    sp->setBackgroundColor(LLColor4(0.039f, 0.059f, 0.090f, 1.f));
                }
            }
        }

        // ---- FPS counter, right before the window buttons ----
        static LLTextBox* sFpsText = NULL;
        if (!sFpsText)
        {
            LLTextBox::Params fp;
            fp.name("hyena_fps");
            fp.rect(LLRect(0, 16, 42, 0));
            fp.follows.flags(FOLLOWS_RIGHT | FOLLOWS_TOP);
            fp.font(LLFontGL::getFontSansSerifSmall());
            fp.font_halign(LLFontGL::RIGHT);
            fp.mouse_opaque(false);
            fp.initial_value(std::string("-- FPS"));
            sFpsText = LLUICtrlFactory::create<LLTextBox>(fp);
            container->addChild(sFpsText);
        }
        {
            const S32 fh = 16;
            LLRect    fr;
            fr.setOriginAndSize(right_limit - 8 - 42, ch - (HYENA_BAR_HEIGHT - fh) / 2 - fh, 42, fh);
            if (sFpsText->getRect() != fr)
            {
                sFpsText->setRect(fr);
            }
            sFpsText->setVisible(fps_on);
            static LLTimer sFpsTimer;
            if (sFpsTimer.getElapsedTimeF32() >= 0.5f)
            {
                sFpsTimer.reset();
                const S32 fps = (S32)llround(LLTrace::get_frame_recording().getPeriodMedianPerSec(LLStatViewer::FPS, 50));
                sFpsText->setText(llformat("%d FPS", fps));
                sFpsText->setColor(fps >= 30 ? LLColor4(0.21f, 0.88f, 0.54f, 1.f)
                    : (fps >= 15 ? LLColor4(1.f, 0.85f, 0.25f, 1.f) : LLColor4(1.f, 0.35f, 0.35f, 1.f)));
            }
        }

        // ---- place the location strip and draw the pills ----
        if (have_loc)
        {
            // Full location, or the short one without the parcel name (the strip is slid left so its parcel
            // icons land right after the short text, and the viewer's own text is hidden)
            if (!sLocText)
            {
                LLTextBox::Params lp;
                lp.name("hyena_loc_text");
                lp.rect(LLRect(12, 19, 200, 0));
                lp.follows.flags(FOLLOWS_LEFT | FOLLOWS_TOP);
                lp.font(hyena_loc_font());
                lp.mouse_opaque(true);
                lp.tool_tip(std::string("About land"));
                lp.text_color(LLColor4(0.90f, 0.94f, 0.97f, 1.f));
                lp.initial_value(std::string());
                sLocText = LLUICtrlFactory::create<LLTextBox>(lp);
                sLocText->setClickedCallback([](void*) { open_about_land(); });
                sInfoHolder->addChild(sLocText);
            }
            // Our text replaces the viewer's own; the viewer's strip stays (slid left) only for its parcel icons
            LLView* place_btn = info ? info->findChildView("place_info_btn", true) : NULL;
            if (info)
            {
                if (info->getRect().mLeft != -compact_shift)
                {
                    info->setOrigin(-compact_shift, 0);
                }
                if (loc_info_text)
                {
                    loc_info_text->setVisible(false);
                }
                if (place_btn)
                {
                    place_btn->setVisible(false);
                }
                const std::string& shown   = compact ? compact_str : full_str;
                const S32          shown_w = compact ? compact_text_w : full_text_w;
                if (sLocText->getText() != shown)
                {
                    sLocText->setText(shown);
                }
                LLRect lr(loc_text_x, loc_h, loc_text_x + shown_w + 4, 0);
                if (sLocText->getRect() != lr)
                {
                    sLocText->setRect(lr);
                }
                sLocText->setVisible(true);
                loc_width = compact ? loc_width_compact : loc_width;
            }
            // The pill reaches out to a small gap before the magnifier (or the L$ group), but never more than
            // 160 px longer than its content, so on a very wide window it does not become a long bar.
            {
                const S32 bal_edge   = sb_right - dist_bal;
                const S32 right_edge = (btn_on ? bal_edge - BTN_GAP - BTN_W : bal_edge) - 8;
                loc_width            = llmin(loc_width, llmax(right_edge - loc_start, 40)); // ends right after the rating
            }
            LLRect place;
            place.setOriginAndSize(loc_start, loc_y, loc_width, loc_h);
            if (sInfoHolder->getRect() != place)
            {
                sInfoHolder->setRect(place);
            }
            sInfoHolder->setVisible(true);
            place_pill(container, "hyena_pill_location", place.mLeft, place.mRight, ch, true);
        }
        {
            LLRect r;
            if (status_item_rect(gStatusBar, "balance_bg", r))
            {
                place_pill(container, "hyena_pill_balance", r.mLeft - 4, r.mRight + 4, ch, true);
            }
            LLTextBox* clock = gStatusBar->findChild<LLTextBox>("TimeText");
            if (clock && status_item_rect(gStatusBar, "TimeText", r))
            {
                place_pill(container, "hyena_pill_time", r.mLeft - 6, r.mLeft + clock->getTextBoundingRect().getWidth() + 8, ch, true);
            }
            S32               il = 100000, ir = -100000;
            bool              any     = false;
            const char* const icons[] = { "presets_icon_camera", "presets_icon_graphic", "stream_toggle_btn", "media_toggle_btn", "volume_btn" };
            for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); ++i)
            {
                if (status_item_rect(gStatusBar, icons[i], r))
                {
                    il  = llmin(il, r.mLeft);
                    ir  = llmax(ir, r.mRight);
                    any = true;
                }
            }
            if (any)
            {
                place_pill(container, "hyena_pill_icons", il - 6, ir + 6, ch, true);
            }
            place_pill(container, "hyena_pill_fps", sFpsText->getRect().mLeft - 8, sFpsText->getRect().mRight + 8, ch, fps_on);
        }

        // ---- the drag strip behind everything in the bar ----
        {
            HyenaDragStrip* strip = container->findChild<HyenaDragStrip>("hyena_drag_strip");
            if (!strip)
            {
                LLView::Params dp;
                dp.name("hyena_drag_strip");
                dp.rect(LLRect(0, ch, cw, ch - HYENA_BAR_HEIGHT));
                dp.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP);
                dp.mouse_opaque(true);
                strip = new HyenaDragStrip(dp);
                container->addChildInBack(strip);
            }
            const LLRect want(0, ch, cw, ch - HYENA_BAR_HEIGHT);
            if (strip->getRect() != want)
            {
                strip->setShape(want, true);
            }
        }

        menu_right = root->getRect().getWidth() - HYENA_BUTTONS_WIDTH; // UI units, converted below
    }

    const S32 menu_right_px = (S32)(menu_right * scale);
    win->setCustomFrame(true, (S32)(HYENA_BAR_HEIGHT * scale), (S32)(menu_left * scale), menu_right_px, (S32)(HYENA_BUTTONS_WIDTH * scale));
}
#endif

// ---------------------------------------------------------------------
//  AUTO-FIT: the first time the viewer runs on a screen (or when the screen resolution changes) the UI size is
//  chosen from the monitor, so the launcher and the bar look right on any display. Windows' own display scaling is
//  always respected (never made smaller). After that the person's own UI Size choice is left alone.
// ---------------------------------------------------------------------
#if LL_WINDOWS
static void auto_fit_ui_scale()
{
    static bool sDone = false;
    if (sDone || !gViewerWindow || !gViewerWindow->getWindow())
    {
        return;
    }
    HWND hwnd = (HWND)gViewerWindow->getPlatformWindow();
    if (!hwnd)
    {
        return;
    }
    sDone = true;

    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        return;
    }
    const S32 mon_w = mi.rcMonitor.right - mi.rcMonitor.left;
    const S32 mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (mon_w <= 0 || mon_h <= 0)
    {
        return;
    }
    const F32 system_scale = llmax(gViewerWindow->getWindow()->getSystemUISize(), 0.1f);

    const std::string signature = llformat("%dx%d@%.2f", mon_w, mon_h, system_scale);
    if (!gSavedSettings.controlExists("HyenaScreenSignature"))
    {
        gSavedSettings.declareString("HyenaScreenSignature", "", "Screen the UI size was last fitted to",
            LLControlVariable::PERSIST_ALWAYS);
    }
    if (gSavedSettings.getString("HyenaScreenSignature") == signature)
    {
        return; // already fitted to this screen
    }

    // 1080p is the reference. Smaller screens get a smaller UI (but not under 0.75), bigger screens a bigger one.
    F32 wanted       = llmin(mon_h / 1080.f, mon_w / 1920.f);
    wanted           = llclamp(wanted, 0.75f, 2.0f);
    wanted           = ll_round(wanted * 8.f) / 8.f; // steps of 0.125
    wanted           = llmax(wanted, system_scale);  // never smaller than what Windows' scaling already gives
    const F32 factor = llclamp(wanted / system_scale, 0.75f, 2.0f);

    gSavedSettings.setString("HyenaScreenSignature", signature);
    if (fabsf(gSavedSettings.getF32("UIScaleFactor") - factor) > 0.01f)
    {
        gSavedSettings.setF32("UIScaleFactor", factor);
        gViewerWindow->reshape(gViewerWindow->getWindowWidthRaw(), gViewerWindow->getWindowHeightRaw());
    }
    LL_INFOS("HyenaDebug") << "HYENA: UI size fitted to " << signature << " -> UIScaleFactor " << factor << LL_ENDL;
}
#endif

// Runs every frame while the launcher is up: refreshes the clocks twice a second
// and the live data every five minutes.
static void launcher_tick(LLPanel* panel)
{
    // The launcher is only hidden (not deleted) once you are logged in. Do no work, and make
    // no web requests, while it is out of sight.
    poll_destination_counts(); // works in-world too, while the launcher is hidden
#if LL_WINDOWS
    auto_fit_ui_scale();

    // Preferences > Voice open: note the state of the two device boxes (once a second, only while it is open)
    {
        static U32         sTick = 0;
        static std::string sLast;
        if ((++sTick % 60) == 0)
        {
            LLFloater* pref = LLFloaterReg::findInstance("preferences");
            if (pref && pref->getVisible())
            {
                LLComboBox* in  = pref->findChild<LLComboBox>("voice_input_device", true);
                LLComboBox* out = pref->findChild<LLComboBox>("voice_output_device", true);
                if (!in || !out)
                {
                    const std::string nf = "boxes not found by name";
                    if (nf != sLast)
                    {
                        sLast = nf;
                        LL_INFOS("HyenaDebug") << "HYENA: voice boxes not found in Preferences (in=" << (int)(in != NULL)
                            << " out=" << (int)(out != NULL) << ")" << LL_ENDL;
                    }
                }
                if (in && out)
                {
                    const std::string now =
                        llformat("in: enabled=%d visible=%d items=%d | out: enabled=%d items=%d", (int)in->getEnabled(),
                            (int)in->isInVisibleChain(), (int)in->getItemCount(), (int)out->getEnabled(), (int)out->getItemCount());
                    if (now != sLast)
                    {
                        sLast = now;
                        LL_INFOS("HyenaDebug") << "HYENA: voice boxes " << now << LL_ENDL;
                    }
                }
            }
        }
    }

    // Voice device boxes: the list can come back empty while voice is on (it fills again only when voice restarts). While
    // Preferences is open and the list is empty, ask the voice system to scan the devices again every few seconds.
    {
        static F64 sNextCheck = 0.0;
        static F64 sNextTry   = 0.0;
        static S32 sTries     = 0;
        const F64  now        = LLTimer::getTotalSeconds();
        if (now >= sNextCheck)
        {
            sNextCheck    = now + 1.0;
            LLFloater* pf = LLFloaterReg::findInstance("preferences");
            if (pf && pf->getVisible() && LLVoiceClient::instanceExists() && LLVoiceClient::getInstance()->voiceEnabled() &&
                !LLVoiceClient::getInstance()->deviceSettingsAvailable())
            {
                if (now >= sNextTry && sTries < 8)
                {
                    sNextTry = now + 3.0;
                    ++sTries;
                    LL_INFOS("HyenaDebug") << "HYENA: voice device list is empty, scanning again (try " << sTries << ")" << LL_ENDL;
                    LLVoiceClient::getInstance()->refreshDeviceLists(false);
                }
            }
            else
            {
                sTries = 0;
            }
        }
    }

    // Diagnostic for the greyed-out voice device boxes: write a log line whenever the voice device state changes
    {
        static S32 sLastState = -1;
        if (LLVoiceClient::instanceExists())
        {
            const bool avail = LLVoiceClient::getInstance()->deviceSettingsAvailable();
            const S32  caps  = (S32)LLVoiceClient::getInstance()->getCaptureDevices().size();
            const S32  outs  = (S32)LLVoiceClient::getInstance()->getRenderDevices().size();
            const S32  state = (avail ? 1 : 0) + caps * 10 + outs * 1000;
            if (state != sLastState)
            {
                sLastState = state;
                LL_INFOS("HyenaDebug") << "HYENA: voice devices available=" << avail << " mics=" << caps << " speakers=" << outs
                    << " voice_enabled=" << LLVoiceClient::getInstance()->voiceEnabled() << LL_ENDL;
            }
        }
    }
    {
        // The parcel icons (no fly / no build / no push / voice ...) and the coordinates show only when these two
        // viewer settings are on. Turn them on once; after that it is the person's choice (World > Show).
        static bool sBarDefaults = false;
        if (!sBarDefaults)
        {
            sBarDefaults = true;
            if (!gSavedSettings.controlExists("HyenaBarDefaultsApplied"))
            {
                gSavedSettings.declareBOOL("HyenaBarDefaultsApplied", false, "Bar defaults have been applied once",
                    LLControlVariable::PERSIST_ALWAYS);
            }
            if (!gSavedSettings.getBOOL("HyenaBarDefaultsApplied"))
            {
                gSavedSettings.setBOOL("NavBarShowParcelProperties", true);
                gSavedSettings.setBOOL("NavBarShowCoordinates", true);
                gSavedSettings.setBOOL("HyenaBarDefaultsApplied", true);
            }
        }
    }
    apply_custom_frame(panel); // our title bar, on the login screen and in-world
#endif
    if (!panel->getVisible())
    {
        return;
    }
    sync_region_hint(panel);
    update_button_feedback(panel);

    static LLTimer clock_timer;
    static LLTimer refresh_timer;
    if (clock_timer.getElapsedTimeF32() >= 0.5f)
    {
        clock_timer.reset();
        update_clocks(panel);
    }
    if (refresh_timer.getElapsedTimeF32() >= 300.f)
    {
        refresh_timer.reset();
        start_grid_fetch(panel);
    }
}

// --- ADD THIS PARAMS CONSTRUCTOR ---
LLLauncherFloater::Params::Params()
{
}
// -----------------------------------

// --- UPDATE THIS CONSTRUCTOR ---
LLLauncherFloater::LLLauncherFloater(const Params& p) : LLPanel(p)
{
    mCommitCallbackRegistrar.add("Launcher.Launch", [this](LLUICtrl* ctrl, const LLSD& userdata) { onLaunchClicked(); });
    mCommitCallbackRegistrar.add("Launcher.ForgotPassword",
        [](LLUICtrl* ctrl, const LLSD& userdata) { LLWeb::loadURLExternal(std::string("https://secure.secondlife.com/account/request.php"), true); });
    mCommitCallbackRegistrar.add("Launcher.CreateAccount",
        [](LLUICtrl* ctrl, const LLSD& userdata) { LLWeb::loadURLExternal(std::string("https://join.secondlife.com/"), true); });
}
// -------------------------------

LLLauncherFloater::~LLLauncherFloater()
{
    if (LLEventPumps::instanceExists())
    {
        LLEventPumps::instance().obtain("mainloop").stopListening("HyenaLauncherTick");
    }
}

bool LLLauncherFloater::postBuild()
{
    mAccountCombo       = getChild<LLComboBox>("username_combo");
    mPasswordEdit       = getChild<LLLineEditor>("password_edit");
    mStartLocationCombo = getChild<LLComboBox>("start_location_combo");
    mRememberUserCheck  = getChild<LLCheckBoxCtrl>("remember_username_checkbox");
    mRememberPassCheck  = getChild<LLCheckBoxCtrl>("remember_check");

    // The startup "Initializing" box has done its job once the launcher exists, so make sure it never stays on screen.
    LLSplashScreen::hide();

    // Multi-factor authentication screen (hidden until the login asks for a code)
    mMfaPanel         = findChild<LLPanel>("mfa_panel");
    mMfaCodeEdit      = findChild<LLLineEditor>("mfa_code_edit");
    mMfaRememberCheck = findChild<LLCheckBoxCtrl>("mfa_remember_check");
    mMfaVerifyBtn     = findChild<LLButton>("mfa_verify_btn");
    mMfaError         = findChild<LLTextBox>("mfa_error");
    if (mMfaCodeEdit)
    {
        mMfaCodeEdit->setKeystrokeCallback([](LLLineEditor* caller, void* data) { static_cast<LLLauncherFloater*>(data)->updateMfaButton(); }, this);
        mMfaCodeEdit->setCommitCallback([this](LLUICtrl*, const LLSD&) { onMfaVerify(); }); // Enter key
    }
    // The MFA card lives in a nested panel, which has its own callback scope, so the buttons are wired here.
    if (mMfaVerifyBtn)
    {
        mMfaVerifyBtn->setCommitCallback([this](LLUICtrl*, const LLSD&) { onMfaVerify(); });
    }
    if (LLButton* cancel_btn = findChild<LLButton>("mfa_cancel_btn"))
    {
        cancel_btn->setCommitCallback([this](LLUICtrl*, const LLSD&) { onMfaCancel(); });
    }
    if (LLButton* support_btn = findChild<LLButton>("mfa_support_btn"))
    {
        support_btn->setCommitCallback([](LLUICtrl*, const LLSD&) { LLWeb::loadURLExternal(std::string("https://support.secondlife.com"), true); });
    }
    if (mMfaPanel)
    {
        mMfaPanel->setVisible(false);
    }

    // Time zone drop-down of the second clock (the choice is remembered between runs)
    LLComboBox* tz_combo = findChild<LLComboBox>("tz_combo");
    if (tz_combo)
    {
        if (!gSavedSettings.controlExists("HyenaClockZone"))
        {
            gSavedSettings.declareString("HyenaClockZone",
                "local",
                "Time zone of the launcher's second clock",
                LLControlVariable::PERSIST_ALWAYS);
        }
        sClockZoneId         = gSavedSettings.getString("HyenaClockZone");
        const int local_zone = find_local_zone_index();
        if (sClockZoneId != "local" &&
            (!find_zone(sClockZoneId) || sClockZoneId == "utc" || (local_zone >= 0 && sClockZoneId == CLOCK_ZONES[local_zone].id)))
        {
            sClockZoneId = "local"; // unknown, or the same as this PC's own zone
        }
        build_zone_menu(tz_combo, sClockZoneId);
        tz_combo->setCommitCallback(
            [this](LLUICtrl* ctrl, const LLSD&)
            {
                LLComboBox* combo = dynamic_cast<LLComboBox*>(ctrl);
                if (combo)
                {
                    const std::string picked = combo->getValue().asString();
                    if (!picked.empty())
                    {
                        sClockZoneId = picked;
                        gSavedSettings.setString("HyenaClockZone", picked);
                    }
                }
                update_clocks(this);
            });
    }

    // Pre-flight maintenance buttons
    LLButton* flush_btn   = findChild<LLButton>("preflight_flush_btn");
    LLButton* reindex_btn = findChild<LLButton>("preflight_reindex_btn");
    LLButton* voice_btn   = findChild<LLButton>("preflight_voice_btn");
    LLButton* ping_btn    = findChild<LLButton>("preflight_ping_btn");
    if (flush_btn)
    {
        flush_btn->setCommitCallback([](LLUICtrl*, const LLSD&)
            { LLNotificationsUtil::add("ConfirmClearCache", LLSD(), LLSD(), flush_cache_callback); });
    }
    if (reindex_btn)
    {
        reindex_btn->setCommitCallback(
            [this](LLUICtrl*, const LLSD&)
            {
                const int removed = delete_inventory_cache_files();
                set_button_feedback(this, "preflight_reindex_btn", removed > 0 ? "Done" : "Nothing to clear");
            });
    }
    if (voice_btn)
    {
        voice_btn->setCommitCallback(
            [this](LLUICtrl*, const LLSD&)
            {
                reset_voice_settings();
                set_button_feedback(this, "preflight_voice_btn", "Reset");
            });
    }
    if (ping_btn)
    {
        ping_btn->setCommitCallback([this](LLUICtrl*, const LLSD&) { start_ping(this); });
    }

    // Destination guide: the tabs, the three cards and SET AS TARGET
    load_destinations();
    for (int i = 0; i < DEST_TAB_COUNT; ++i)
    {
        LLButton* tab = findChild<LLButton>(DEST_TAB_BUTTONS[i]);
        if (tab)
        {
            tab->setCommitCallback(
                [this, i](LLUICtrl*, const LLSD&)
                {
                    sDestTab        = i;
                    sDestSelected   = 0; // clicking a tab picks its first destination
                    sDestUserPicked = true;
                    show_destinations(this);
                });
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        LLButton* card = findChild<LLButton>(llformat("dest_card%d_btn", i + 1));
        if (card)
        {
            card->setCommitCallback(
                [this, i](LLUICtrl*, const LLSD&)
                {
                    sDestSelected   = i;
                    sDestUserPicked = true;
                    show_destinations(this);
                });
        }
    }
    // The saved-accounts drop-down: picking an account fills in its name, password dots and favorites
    if (mAccountCombo)
    {
        mAccountCombo->setCommitCallback(
            [this](LLUICtrl*, const LLSD&)
            {
                if (mAccountCombo->getCurrentIndex() != -1)
                {
                    const std::string       key = mAccountCombo->getSelectedValue().asString();
                    LLPointer<LLCredential> cred =
                        gSecAPIHandler->loadFromCredentialMap("login_list", LLGridManager::getInstance()->getGrid(), key);
                    if (cred.notNull())
                    {
                        const bool has_password = cred->getAuthenticator().isMap() && cred->getAuthenticator().has("secret");
                        populateFields(cred, true, has_password);
                    }
                }
                else if (mStartLocationCombo)
                {
                    load_start_locations(mStartLocationCombo, mAccountCombo->getSimple());
                    update_region_box(this, mStartLocationCombo, false);
                }
            });
        mAccountCombo->setTextChangedCallback(
            [this](LLUICtrl*, const LLSD&)
            {
                if (mPasswordEdit && mPasswordEdit->getText() == PASSWORD_FILLER)
                {
                    mPasswordEdit->setText(std::string()); // typing a different name: the remembered password is not for it
                }
            });
    }

#if LL_WINDOWS
    // Our own minimize / maximize / close buttons in the title bar
    LLButton* win_min = findChild<LLButton>("win_min_btn");
    if (win_min)
    {
        win_min->setCommitCallback([](LLUICtrl*, const LLSD&) { gViewerWindow->getWindow()->minimize(); });
    }
    LLButton* win_max = findChild<LLButton>("win_max_btn");
    if (win_max)
    {
        win_max->setCommitCallback(
            [](LLUICtrl*, const LLSD&)
            {
                LLWindow* window = gViewerWindow->getWindow();
                if (window->getMaximized())
                {
                    window->restore();
                }
                else
                {
                    window->maximize();
                }
            });
    }
    LLButton* win_close = findChild<LLButton>("win_close_btn");
    if (win_close)
    {
        win_close->setCommitCallback([](LLUICtrl*, const LLSD&) { LLAppViewer::instance()->userQuit(); });
    }
#endif
    LLButton* target_btn = findChild<LLButton>("dest_target_btn");
    if (target_btn)
    {
        target_btn->setCommitCallback([this](LLUICtrl*, const LLSD&) { set_destination_target(this, mStartLocationCombo); });
    }
    show_destinations(this);

    // Top half: live clocks, grid status and news
    update_clocks(this);
    start_grid_fetch(this);
    {
        LLEventPump& mainloop = LLEventPumps::instance().obtain("mainloop");
        mainloop.stopListening("HyenaLauncherTick");
        mainloop.listen("HyenaLauncherTick",
            [this](const LLSD&)
            {
                // timing: how much of each frame does our code take? (logged every 10 s)
                const F64 t0 = LLTimer::getTotalSeconds();
                launcher_tick(this);
                const F64  dt   = LLTimer::getTotalSeconds() - t0;
                static F64 sSum = 0.0, sMax = 0.0, sNextLog = 0.0;
                static S32 sFrames = 0;
                sSum += dt;
                sMax = llmax(sMax, dt);
                ++sFrames;
                if (t0 >= sNextLog)
                {
                    if (sNextLog > 0.0)
                    {
                        LL_INFOS("HyenaDebug")
                            << "HYENA: our per-frame code: avg " << (sSum / sFrames * 1000.0) << " ms, worst "
                            << (sMax * 1000.0) << " ms, frames in last 10 s: " << sFrames << LL_ENDL;
                    }
                    sNextLog = t0 + 10.0;
                    sSum = sMax = 0.0;
                    sFrames     = 0;
                }
                return false;
            });
    }

    // Typing a different account name loads that account's favorites
    LLLineEditor* username_box = findChild<LLLineEditor>("username_edit");
    if (username_box)
    {
        username_box->setCommitCallback(
            [this](LLUICtrl* ctrl, const LLSD&)
            {
                LLLineEditor* box = dynamic_cast<LLLineEditor*>(ctrl);
                if (mPasswordEdit && mPasswordEdit->getText() == PASSWORD_FILLER)
                {
                    mPasswordEdit->setText(std::string()); // the remembered password belongs to the old name
                }
                if (box && mStartLocationCombo)
                {
                    load_start_locations(mStartLocationCombo, box->getText());
                    update_region_box(this, mStartLocationCombo, false);
                }
            });
    }

    // Show the region box only while "Custom Region" is picked
    if (mStartLocationCombo)
    {
        mStartLocationCombo->setCommitCallback([this](LLUICtrl*, const LLSD&) { update_region_box(this, mStartLocationCombo, true); });
        update_region_box(this, mStartLocationCombo, false);

        // The arrow inside the typing box goes back to the drop-down
        LLButton* region_arrow = findChild<LLButton>("region_arrow_btn");
        if (region_arrow)
        {
            region_arrow->setCommitCallback(
                [this](LLUICtrl*, const LLSD&)
                {
                    if (mStartLocationCombo)
                    {
                        mStartLocationCombo->setValue(LLSD(sLastListChoice.empty() ? std::string("home") : sLastListChoice));
                        update_region_box(this, mStartLocationCombo, false);
                    }
                });
        }
    }

    // Show / hide password. Two plain buttons share one spot: the open eye shows the password,
    // the slashed eye hides it again. Only one of them is visible at a time.
    LLButton* show_pass_btn = findChild<LLButton>("show_password_btn");
    LLButton* hide_pass_btn = findChild<LLButton>("hide_password_btn");
    if (show_pass_btn && hide_pass_btn && mPasswordEdit)
    {
        show_pass_btn->setMouseDownCallback([this](LLUICtrl*, const LLSD&)
            { setLauncherPasswordVisible(this, mPasswordEdit, true, false); });
        hide_pass_btn->setMouseDownCallback([this](LLUICtrl*, const LLSD&)
            { setLauncherPasswordVisible(this, mPasswordEdit, false, false); });
    }

    return true;
}

// ---- Multi-factor authentication screen ----
static bool sMfaTried = false; // a code was already sent for this login, so a new challenge means it was wrong

static LLLauncherFloater* find_launcher()
{
    if (!gViewerWindow || !gViewerWindow->getRootView())
    {
        return NULL;
    }
    return dynamic_cast<LLLauncherFloater*>(gViewerWindow->getRootView()->findChildView("hyena_launcher", true));
}

// static
bool LLLauncherFloater::showMFA(const std::string& message)
{
    LLLauncherFloater* launcher = find_launcher();
    if (!launcher || !launcher->mMfaPanel || !launcher->mMfaCodeEdit || !launcher->isInVisibleChain())
    {
        return false; // the caller falls back to the viewer's own MFA window
    }
    if (launcher->mMfaError)
    {
        if (sMfaTried)
        {
            launcher->mMfaError->setText(std::string("That code was not accepted. Check your authenticator app and try again."));
            launcher->mMfaError->setVisible(true);
        }
        else
        {
            launcher->mMfaError->setVisible(false);
        }
    }
    launcher->mMfaCodeEdit->setText(std::string());
    launcher->updateMfaButton();
    launcher->mMfaPanel->setVisible(true);
    launcher->mMfaCodeEdit->setFocus(true);
    return true;
}

void LLLauncherFloater::hideMFA()
{
    if (mMfaPanel)
    {
        mMfaPanel->setVisible(false);
    }
}

void LLLauncherFloater::updateMfaButton()
{
    if (!mMfaCodeEdit || !mMfaVerifyBtn)
    {
        return;
    }
    const std::string code = mMfaCodeEdit->getText();
    bool              six  = (code.size() == 6);
    for (char ch : code)
    {
        if (ch < '0' || ch > '9')
        {
            six = false;
        }
    }
    mMfaVerifyBtn->setEnabled(six);
}

void LLLauncherFloater::onMfaVerify()
{
    if (!mMfaCodeEdit)
    {
        return;
    }
    std::string code;
    for (char ch : mMfaCodeEdit->getText())
    {
        if (ch != ' ')
        {
            code.push_back(ch);
        }
    }
    bool six = (code.size() == 6);
    for (char ch : code)
    {
        if (ch < '0' || ch > '9')
        {
            six = false;
        }
    }
    if (!six)
    {
        return; // exactly six digits
    }
    const bool remember = mMfaRememberCheck && mMfaRememberCheck->get();
    sMfaTried           = true;
    mMfaCodeEdit->setText(std::string());
    hideMFA();
    LLLoginInstance::getInstance()->submitExternalMFAResponse(code, remember);
}

void LLLauncherFloater::onMfaCancel()
{
    sMfaTried = false;
    if (mMfaCodeEdit)
    {
        mMfaCodeEdit->setText(std::string());
    }
    hideMFA();
    LLLoginInstance::getInstance()->cancelExternalMFA();
}

void LLLauncherFloater::onLaunchClicked()
{
    sMfaTried = false; // a new login starts
    LL_INFOS() << "Launch button clicked! Ready to log in." << LL_ENDL;

    // Same thing login_callback() does for the connect button. Only act while we are
    // waiting for the user, so a double click can't restart the login.
    if (LLStartUp::getStartupState() == STATE_LOGIN_WAIT)
    {
        apply_start_location(this, mStartLocationCombo);
        LLStartUp::setStartupState(STATE_LOGIN_CLEANUP);
    }
}

void LLLauncherFloater::populateFields(LLPointer<LLCredential> credential, bool remember_user, bool remember_password)
{
    // Always come back to the login screen with the password hidden
    setLauncherPasswordVisible(this, mPasswordEdit, false, false);

    if (mRememberUserCheck)
    {
        mRememberUserCheck->set(remember_user);
    }
    if (mRememberPassCheck)
    {
        mRememberPassCheck->set(remember_password);
    }

    load_start_locations(mStartLocationCombo, credential.notNull() ? credential->userID() : std::string());
    update_region_box(this, mStartLocationCombo, false);

    // A remembered password shows as dots in the box, like the official viewer does
    if (mPasswordEdit)
    {
        const bool has_saved_password = remember_password && credential.notNull() && credential->getAuthenticator().isMap() &&
            credential->getAuthenticator().has("secret");
        mPasswordEdit->setText(has_saved_password ? std::string(PASSWORD_FILLER) : std::string());
    }

    if (credential.notNull())
    {
        LLLineEditor* user_edit = findChild<LLLineEditor>("username_edit");
        if (user_edit)
        {
            user_edit->setText(username_for_display(credential->userID()));
        }
    }

    // More than one saved account: show the drop-down with the current one picked
    if (mAccountCombo)
    {
        const int saved = load_saved_accounts(mAccountCombo);
        show_account_controls(this, saved);
        if (credential.notNull() && saved > 0)
        {
            if (!mAccountCombo->setSelectedByValue(LLSD(credential->userID()), true))
            {
                mAccountCombo->setSimple(username_for_display(credential->userID()));
            }
        }
    }
}

void LLLauncherFloater::getFields(LLPointer<LLCredential>& user_credential, bool& remember_user, bool& remember_password)
{
    remember_user     = mRememberUserCheck->get();
    remember_password = mRememberPassCheck->get();

    LLLineEditor* user_edit = findChild<LLLineEditor>("username_edit");
    std::string   username  = user_edit ? user_edit->getText() : mAccountCombo->getValue().asString();
    if (mAccountCombo && mAccountCombo->getVisible())
    {
        username = mAccountCombo->getSimple(); // the drop-down is in use
    }
    LLStringUtil::trim(username);
    std::string password = mPasswordEdit->getText();
    if (password == PASSWORD_FILLER)
    {
        password.clear(); // untouched filler: keep the saved password
    }

    // "first last", "first.last", "first_last" or just "first" (last name defaults to resident)
    std::string::size_type sep   = username.find_first_of(" ._");
    std::string            first = username.substr(0, sep);
    std::string            last  = "resident";
    if (sep != std::string::npos)
    {
        last = username.substr(sep + 1);
        LLStringUtil::trim(last);
        if (last.empty())
        {
            last = "resident";
        }
    }

    LLSD identifier          = LLSD::emptyMap();
    identifier["type"]       = "agent";
    identifier["first_name"] = first;
    identifier["last_name"]  = last;

    LLSD authenticator = LLSD::emptyMap();
    if (!password.empty())
    {
        LLMD5 pass((unsigned char*)password.c_str());
        char  md5pass[33];
        pass.hex_digest(md5pass);
        authenticator["type"]      = "hash";
        authenticator["algorithm"] = "md5";
        authenticator["secret"]    = md5pass;
    }
    else if (user_credential.notNull())
    {
        // Password box left empty: keep the saved password for this account
        authenticator = user_credential->getAuthenticator();
    }

    user_credential = gSecAPIHandler->createCredential(LLGridManager::getInstance()->getGrid(), identifier, authenticator);

    // The LAUNCH button is handled in llstartup.cpp (it only moves the login on to STATE_LOGIN_CLEANUP),
    // so onLaunchClicked() above never runs. This function IS called there, right before the login
    // starts, which makes it the place where the chosen start location (Home, Last Location,
    // Custom Region or a favorite) is handed to the login.
    apply_start_location(this, mStartLocationCombo);
}

void LLLauncherFloater::giveFocus()
{
    LLLineEditor* user_edit = findChild<LLLineEditor>("username_edit");
    if (user_edit && user_edit->getText().empty())
    {
        user_edit->setFocus(true);
    }
    else if (mPasswordEdit)
    {
        mPasswordEdit->setFocus(true);
    }
    else
    {
        setFocus(true);
    }
}

// ADD THIS LINE AT THE VERY BOTTOM
static LLDefaultChildRegistry::Register<LLLauncherFloater> r("hyena_launcher");

// Menu: "Project Hyena Discord" - not live yet
static void hyena_coming_soon(LLUICtrl*, const LLSD&)
{
    LLSD args;
    args["MESSAGE"] = "The Project Hyena Discord is coming soon.";
    LLNotificationsUtil::add("GenericAlert", args);
}
static bool hyena_coming_soon_registered =
(LLUICtrl::CommitCallbackRegistry::defaultRegistrar().add("Hyena.ComingSoon", hyena_coming_soon), true);
