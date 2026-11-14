// The xmake build doesn't force-include pch.h, so include it explicitly (must come first).
#include "pch.h"

// SimpleIni (bundled with ClibUtil). Use its inline UTF-8 converter so it
// doesn't pull in <windows.h>, which clashes with CommonLib's Win32 declarations.
#define SI_CONVERT_GENERIC
// In case a header defined min/max macros before NOMINMAX took effect.
#ifdef max
#	undef max
#endif
#ifdef min
#	undef min
#endif
#include <SimpleIni.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace CalendarManipulator
{
    // ---------------------------------------------------------------------
    // Settings
    // ---------------------------------------------------------------------
    namespace Settings
    {
        constexpr int kDefaultDaysPerMonth = 31;
        constexpr int kMinDaysPerMonth = 1;
        constexpr int kMaxDaysPerMonth = 31;

        inline int  daysPerMonth = kDefaultDaysPerMonth;
        inline bool monthlySeasons = false;
        inline bool displayNotifications = true;

        inline const char* kIniPath = "Data/SKSE/Plugins/CalendarManipulator.ini";

        void Load()
        {
            CSimpleIniA ini;
            ini.SetUnicode();

            const auto rc = ini.LoadFile(kIniPath);
            if (rc < 0) {
                SKSE::log::warn("Could not read {}, creating it with defaults", kIniPath);
            }

            auto value = static_cast<int>(ini.GetLongValue("General", "daysPerMonth", kDefaultDaysPerMonth));
            if (value < kMinDaysPerMonth || value > kMaxDaysPerMonth) {
                SKSE::log::warn("daysPerMonth={} is out of range [{}, {}], clamping",
                    value, kMinDaysPerMonth, kMaxDaysPerMonth);
                value = std::clamp(value, kMinDaysPerMonth, kMaxDaysPerMonth);
            }
            daysPerMonth = value;

            monthlySeasons = ini.GetBoolValue("General", "MonthlySeasons", false);
            displayNotifications = ini.GetBoolValue("General", "DisplayNotifications", true);

            // Write the file back so a missing INI (or missing keys) gets created with comments.
            bool dirty = rc < 0;
            if (!ini.KeyExists("General", "daysPerMonth")) {
                ini.SetLongValue("General", "daysPerMonth", daysPerMonth,
                    "; Number of days each in-game month lasts (1-31).\n"
                    "; When the day of the month exceeds this value, the calendar jumps to the 1st of the next month.\n"
                    "; Note: months that naturally have fewer days (e.g. Sun's Dawn = 28) are unaffected if this is larger.");
                dirty = true;
            }
            if (!ini.KeyExists("General", "MonthlySeasons")) {
                ini.SetBoolValue("General", "MonthlySeasons", monthlySeasons,
                    "; Compatibility for mods that make each month a season (e.g. Faster Seasons).\n"
                    "; When true, the year advances on every Winter -> Spring transition\n"
                    "; (Sun's Dawn -> First Seed, Mid Year -> Sun's Height, Frostfall -> Sun's Dusk)\n"
                    "; instead of at Evening Star -> Morning Star.");
                dirty = true;
            }
            if (!ini.KeyExists("General", "DisplayNotifications")) {
                ini.SetBoolValue("General", "DisplayNotifications", displayNotifications,
                    "; When true, shows a notification on the 1st of every new month.\n"
                    "; With MonthlySeasons enabled, the message describes the new season instead.");
                dirty = true;
            }
            if (dirty) {
                ini.SaveFile(kIniPath);
            }

            SKSE::log::info("daysPerMonth = {}, MonthlySeasons = {}, DisplayNotifications = {}",
                daysPerMonth, monthlySeasons, displayNotifications);
        }
    }

    // ---------------------------------------------------------------------
    // Calendar logic (must run on the main thread)
    // ---------------------------------------------------------------------
    namespace Calendar
    {
        constexpr int kMonths = RE::Calendar::Month::kTotal;

        inline int lastSeenDay = -1;
        inline int lastSeenMonth = -1;
        inline int lastSeenYear = -1;

        // MonthlySeasons mapping (month index % 4):
        //   0 = Autumn, 1 = Winter, 2 = Spring, 3 = Summer
        // e.g. Morning Star (0) Autumn, Sun's Dawn (1) Winter, First Seed (2) Spring, Rain's Hand (3) Summer...
        enum class Season
        {
            kAutumn = 0,
            kWinter = 1,
            kSpring = 2,
            kSummer = 3
        };

        constexpr Season GetSeason(int a_month) { return static_cast<Season>(a_month % 4); }
        constexpr bool   IsWinterToSpring(int a_fromMonth) { return GetSeason(a_fromMonth) == Season::kWinter; }

        // HUD notifications are rendered as HTML text, so <font color> tints the keyword.
        std::string Colored(std::string_view a_text, std::string_view a_hex)
        {
            return std::format("<font color='#{}'>{}</font>", a_hex, a_text);
        }

        void NotifyNewMonth(RE::Calendar* a_calendar, int a_month)
        {
            if (!Settings::displayNotifications) {
                return;
            }

            std::string message;
            if (Settings::monthlySeasons) {
                switch (GetSeason(a_month)) {
                case Season::kSummer:
                    message = std::format("The sun is high and warm, {} is here", Colored("Summer", "FFD700"));
                    break;
                case Season::kAutumn:
                    message = std::format("Cold winds are starting to blow from the north, {} is here", Colored("Autumn", "D2691E"));
                    break;
                case Season::kWinter:
                    message = std::format("Snow covers everything, {} covers the northern regions of Tamriel", Colored("Winter", "A5F2F3"));
                    break;
                case Season::kSpring:
                    message = std::format("The vegetation is lush and green, {} has arrived", Colored("Spring", "4CBB17"));
                    break;
                }
            } else {
                message = std::format("Another month passed, it is now {}", a_calendar->GetMonthName());
            }

            RE::SendHUDMessage::ShowHUDMessage(message.c_str());
        }

        // Skips to the 1st of the next month if the day exceeds daysPerMonth.
        void ApplyDaySkip(RE::Calendar* a_calendar, int a_day)
        {
            if (a_day <= Settings::daysPerMonth) {
                return;
            }

            const auto oldMonthName = a_calendar->GetMonthName();

            int month = static_cast<int>(a_calendar->gameMonth->value) + 1;
            int year = static_cast<int>(a_calendar->gameYear->value);
            if (month >= kMonths) {
                month = RE::Calendar::Month::kMorningStar;
                ++year;  // vanilla behaviour; MonthlySeasons overrides it in ApplySeasonalYear
            }

            a_calendar->gameDay->value = 1.0f;
            a_calendar->gameMonth->value = static_cast<float>(month);
            a_calendar->gameYear->value = static_cast<float>(year);

            SKSE::log::info("Day {} of {} exceeds {} -> skipped to 1st of {}",
                a_day, oldMonthName, Settings::daysPerMonth, a_calendar->GetMonthName());
        }

        // With MonthlySeasons, the year is driven by Winter -> Spring transitions only.
        // Handles month changes from our own skip and from the engine's natural rollover.
        void ApplySeasonalYear(RE::Calendar* a_calendar, int a_steps)
        {
            if (!Settings::monthlySeasons) {
                return;
            }

            int year = lastSeenYear;
            for (int i = 0, m = lastSeenMonth; i < a_steps; ++i, m = (m + 1) % kMonths) {
                if (IsWinterToSpring(m)) {
                    ++year;
                }
            }

            if (static_cast<int>(a_calendar->gameYear->value) != year) {
                a_calendar->gameYear->value = static_cast<float>(year);
                SKSE::log::info("MonthlySeasons: now {} of year {}", a_calendar->GetMonthName(), year);
            }
        }

        void CheckAndSkip()
        {
            const auto calendar = RE::Calendar::GetSingleton();
            if (!calendar || !calendar->gameDay || !calendar->gameMonth || !calendar->gameYear) {
                return;  // main menu / loading: globals not bound yet
            }

            const int day = static_cast<int>(calendar->gameDay->value);
            if (day != lastSeenDay) {
                ApplyDaySkip(calendar, day);
            }

            const int month = static_cast<int>(calendar->gameMonth->value);  // 0-based (0 = Morning Star)

            // React to month changes (from our skip or the engine's natural rollover).
            // Skipped right after a load (lastSeenMonth < 0) so loading a save doesn't trigger anything.
            if (lastSeenMonth >= 0 && month != lastSeenMonth) {
                const int steps = (month - lastSeenMonth + kMonths) % kMonths;
                if (steps <= 2) {
                    ApplySeasonalYear(calendar, steps);
                    NotifyNewMonth(calendar, month);
                } else {
                    // Not a natural progression (console edit, etc.): just resync.
                    SKSE::log::info("Month jumped from {} to {}, ignoring", lastSeenMonth, month);
                }
            }

            lastSeenDay = static_cast<int>(calendar->gameDay->value);
            lastSeenMonth = month;
            lastSeenYear = static_cast<int>(calendar->gameYear->value);
        }

        // Forgets the tracked date (e.g. after loading a save) so the next check just resyncs.
        void Reset()
        {
            lastSeenDay = -1;
            lastSeenMonth = -1;
            lastSeenYear = -1;
        }
    }

    // ---------------------------------------------------------------------
    // Poller: a lightweight background thread that queues the check onto the
    // game's main thread. The engine has no "new day" event, so we detect the
    // day change ourselves; the check is a no-op unless the day actually changed.
    // ---------------------------------------------------------------------
    namespace Poller
    {
        inline std::atomic_bool started{ false };
        inline std::atomic_bool taskPending{ false };

        constexpr auto kInterval = std::chrono::milliseconds(500);

        void Start()
        {
            if (started.exchange(true)) {
                return;
            }

            std::thread([] {
                const auto tasks = SKSE::GetTaskInterface();
                while (true) {
                    std::this_thread::sleep_for(kInterval);
                    // Don't pile up tasks if the main thread is stalled (loading screens, etc.).
                    if (!taskPending.exchange(true)) {
                        tasks->AddTask([] {
                            Calendar::CheckAndSkip();
                            taskPending = false;
                        });
                    }
                }
            }).detach();

            SKSE::log::info("Day poller started");
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            Poller::Start();
            break;
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            Calendar::Reset();
            if (const auto tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([] { Calendar::CheckAndSkip(); });
            }
            break;
        default:
            break;
        }
    }
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    if (!skse) {
        return false;
    }
    SKSE::Init(skse);

    CalendarManipulator::Settings::Load();

    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(CalendarManipulator::OnMessage)) {
        SKSE::log::critical("Failed to register messaging listener");
        return false;
    }

    SKSE::log::info("CalendarManipulator loaded");
    return true;
}
