"""DATE & TIME (es-rgds-datetime.patch) sets the clock by hand and links a time zone.

The 31st of a short month has to stay in that month, and a time zone from the settings file
is passed to a shell. The patch is applied when EmulationStation is built, so these tests run
the same calendar step and read the checks out of the patch that the build applies.
"""
import re
import time
import unittest
from pathlib import Path

from loadmod import ROOT

PATCH = (ROOT / "dii-ess-aye" / "es-rgds-datetime.patch").read_text()


def clamp_day(year, month, day):
    """The patch's step: day 0 of the next month is this month's last day (mktime normalises it)."""
    last = time.localtime(time.mktime((year, month + 1, 0, 12, 0, 0, 0, 0, -1)))
    return min(day, last.tm_mday)


def tz_charset():
    m = re.search(r'find_first_of\("((?:\\.|[^"\\])*)"\)', PATCH)
    if not m:
        raise AssertionError("the time zone save no longer rejects a character class")
    return bytes(m.group(1), "utf-8").decode("unicode_escape")


def tz_rejected(tz):
    return (not tz) or any(c in tz_charset() for c in tz)


class DateTimePatchTest(unittest.TestCase):
    def test_a_short_month_keeps_its_last_day(self):
        self.assertIn("last.tm_mday = 0", PATCH)
        self.assertIn("std::min(std::stoi(day->getSelected()), last.tm_mday)", PATCH)
        self.assertEqual(clamp_day(2026, 2, 31), 28)
        self.assertEqual(clamp_day(2024, 2, 31), 29)   # a leap year
        self.assertEqual(clamp_day(2026, 4, 31), 30)
        self.assertEqual(clamp_day(2026, 1, 31), 31)
        self.assertEqual(clamp_day(2026, 12, 31), 31)
        self.assertEqual(clamp_day(2026, 6, 5), 5)

    def test_a_clock_before_2025_offers_2026(self):
        self.assertIn("thisYear < 2025 ? 2026 : thisYear", PATCH)

    def test_zones_split_on_newlines_and_a_shell_metacharacter_is_not_linked(self):
        self.assertIn("std::replace(all.begin(), all.end(), '\\n', ',');", PATCH)
        self.assertIn('!Utils::FileSystem::exists("/usr/share/zoneinfo/" + tz)', PATCH)
        # the link runs only after both checks
        save = PATCH.split("tzChoices->getSelected()", 1)[1]
        link = save.find('runSystemCommand("ln -sf')
        self.assertGreater(save.find("find_first_of"), -1)
        self.assertLess(save.find("find_first_of"), link)
        self.assertLess(save.find("zoneinfo/"), link)
        self.assertFalse(tz_rejected("Europe/Stockholm"))
        self.assertFalse(tz_rejected("America/New_York"))
        for bad in ["", "Europe/Stockholm;reboot", "America/New York", "Foo`bar",
                    "$(reboot)", "A'B", 'A"B', "A$B", "A&B", "A\\B"]:
            self.assertTrue(tz_rejected(bad), bad)
