/*
 *	libxt_time - iptables part for xt_time
 *	Copyright © CC Computer Consultants GmbH, 2007
 *	Contact: <jengelh@computergmbh.de>
 *
 *	libxt_time.c is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 or 3 of the License.
 *
 *	Based on libipt_time.c.
 */
#include <sys/types.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <linux/netfilter/xt_time.h>
#include <xtables.h>
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

enum { /* getopt "seen" bits */
	F_DATE_START = 1 << 0,
	F_DATE_STOP  = 1 << 1,
	F_TIME_START = 1 << 2,
	F_TIME_STOP  = 1 << 3,
	F_MONTHDAYS  = 1 << 4,
	F_WEEKDAYS   = 1 << 5,
	F_TIMEZONE   = 1 << 6,
};

static const char *const week_days[] = {
	NULL, "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
};

static const struct option time_opts[] = {
	{"datestart", true,  NULL, 'D'},
	{"datestop",  true,  NULL, 'E'},
	{"timestart", true,  NULL, 'X'},
	{"timestop",  true,  NULL, 'Y'},
	{"weekdays",  true,  NULL, 'w'},
	{"monthdays", true,  NULL, 'm'},
	{"localtz",   false, NULL, 'l'},
	{"utc",       false, NULL, 'u'},
	{ .name = NULL }
};

static void time_help(void)
{
	printf(
"time match options:\n"
"  --datestart time     Start and stop time, to be given in ISO 8601\n"
"  --datestop time      (YYYY[-MM[-DD[Thh[:mm[:ss]]]]])\n"
"  --timestart time     Start and stop daytime (hh:mm[:ss])\n"
"  --timestop time      (between 00:00:00 and 23:59:59)\n"
"  --monthdays value    List of days on which to match, separated by comma\n"
"                       (Possible days: 1 to 31; defaults to all)\n"
"  --weekdays value     List of weekdays on which to match, sep. by comma\n"
"                       (Possible days: Mon,Tue,Wed,Thu,Fri,Sat,Sun or 1 to 7\n"
"                       Defaults to all weekdays.)\n"
"  --localtz/--utc      Time is interpreted as UTC/local time\n");
}

static void time_init(struct xt_entry_match *m)
{
	struct xt_time_info *info = (void *)m->data;

	/* By default, we match on every day, every daytime */
	info->monthdays_match = XT_TIME_ALL_MONTHDAYS;
	info->weekdays_match  = XT_TIME_ALL_WEEKDAYS;
	info->daytime_start   = XT_TIME_MIN_DAYTIME;
	info->daytime_stop    = XT_TIME_MAX_DAYTIME;

	/* ...and have no date-begin or date-end boundary */
	info->date_start = 0;
	info->date_stop  = INT_MAX;

	/* local time is default */
	info->flags |= XT_TIME_LOCAL_TZ;
}

static time_t time_parse_date(const char *s, bool end)
{
	unsigned int month = 1, day = 1, hour = 0, minute = 0, second = 0;
	unsigned int year  = end ? 2038 : 1970;
	const char *os = s;
	struct tm tm;
	time_t ret;
	char *e;

	year = strtoul(s, &e, 10);
	if ((*e != '-' && *e != '\0') || year < 1970 || year > 2038)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	month = strtoul(s, &e, 10);
	if ((*e != '-' && *e != '\0') || month > 12)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	day = strtoul(s, &e, 10);
	if ((*e != 'T' && *e != '\0') || day > 31)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	hour = strtoul(s, &e, 10);
	if ((*e != ':' && *e != '\0') || hour > 23)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	minute = strtoul(s, &e, 10);
	if ((*e != ':' && *e != '\0') || minute > 59)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	second = strtoul(s, &e, 10);
	if (*e != '\0' || second > 59)
		goto out;

 eval:
	tm.tm_year = year - 1900;
	tm.tm_mon  = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min  = minute;
	tm.tm_sec  = second;
	ret = mktime(&tm);
	if (ret >= 0)
		return ret;
	perror("mktime");
	exit_error(OTHER_PROBLEM, "mktime returned an error");

 out:
	exit_error(PARAMETER_PROBLEM, "Invalid date \"%s\" specified. Should "
	           "be YYYY[-MM[-DD[Thh[:mm[:ss]]]]]", os);
	return -1;
}

static unsigned int time_parse_minutes(const char *s)
{
	unsigned int hour, minute, second = 0;
	char *e;

	hour = strtoul(s, &e, 10);
	if (*e != ':' || hour > 23)
		goto out;

	s = e + 1;
	minute = strtoul(s, &e, 10);
	if ((*e != ':' && *e != '\0') || minute > 59)
		goto out;
	if (*e == '\0')
		goto eval;

	s = e + 1;
	second = strtoul(s, &e, 10);
	if (*e != '\0' || second > 59)
		goto out;

 eval:
	return 60 * 60 * hour + 60 * minute + second;

 out:
	exit_error(PARAMETER_PROBLEM, "invalid time \"%s\" specified, "
	           "should be hh:mm[:ss] format and within the boundaries", s);
	return -1;
}

static const char *my_strseg(char *buf, unsigned int buflen,
    const char **arg, char delim)
{
	const char *sep;

	if (*arg == NULL || **arg == '\0')
		return NULL;
	sep = strchr(*arg, delim);
	if (sep == NULL) {
		snprintf(buf, buflen, "%s", *arg);
		*arg = NULL;
		return buf;
	}
	snprintf(buf, buflen, "%.*s", (unsigned int)(sep - *arg), *arg);
	*arg = sep + 1;
	return buf;
}

static uint32_t time_parse_monthdays(const char *arg)
{
	char day[3], *err = NULL;
	uint32_t ret = 0;
	unsigned int i;

	while (my_strseg(day, sizeof(day), &arg, ',') != NULL) {
		i = strtoul(day, &err, 0);
		if ((*err != ',' && *err != '\0') || i > 31)
			exit_error(PARAMETER_PROBLEM,
			           "%s is not a valid day for --monthdays", day);
		ret |= 1 << i;
	}

	return ret;
}

static unsigned int time_parse_weekdays(const char *arg)
{
	char day[4], *err = NULL;
	unsigned int i, ret = 0;
	bool valid;

	while (my_strseg(day, sizeof(day), &arg, ',') != NULL) {
		i = strtoul(day, &err, 0);
		if (*err == '\0') {
			if (i == 0)
				exit_error(PARAMETER_PROBLEM,
				           "No, the week does NOT begin with Sunday.");
			ret |= 1 << i;
			continue;
		}

		valid = false;
		for (i = 1; i < ARRAY_SIZE(week_days); ++i)
			if (strncmp(day, week_days[i], 2) == 0) {
				ret |= 1 << i;
				valid = true;
			}

		if (!valid)
			exit_error(PARAMETER_PROBLEM,
			           "%s is not a valid day specifier", day);
	}

	return ret;
}

static int time_parse(int c, char **argv, int invert, unsigned int *flags,
                      const void *entry, struct xt_entry_match **match)
{
	struct xt_time_info *info = (void *)(*match)->data;

	switch (c) {
	case 'D': /* --datestart */
		if (*flags & F_DATE_START)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --datestart twice");
		if (invert)
			exit_error(PARAMETER_PROBLEM,
			           "Unexpected \"!\" with --datestart");
		info->date_start = time_parse_date(optarg, false);
		*flags |= F_DATE_START;
		return 1;
	case 'E': /* --datestop */
		if (*flags & F_DATE_STOP)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --datestop more than once");
		if (invert)
			exit_error(PARAMETER_PROBLEM,
			           "unexpected \"!\" with --datestop");
		info->date_stop = time_parse_date(optarg, true);
		*flags |= F_DATE_STOP;
		return 1;
	case 'X': /* --timestart */
		if (*flags & F_TIME_START)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --timestart more than once");
		if (invert)
			exit_error(PARAMETER_PROBLEM,
			           "Unexpected \"!\" with --timestart");
		info->daytime_start = time_parse_minutes(optarg);
		*flags |= F_TIME_START;
		return 1;
	case 'Y': /* --timestop */
		if (*flags & F_TIME_STOP)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --timestop more than once");
		if (invert)
			exit_error(PARAMETER_PROBLEM,
			           "Unexpected \"!\" with --timestop");
		info->daytime_stop = time_parse_minutes(optarg);
		*flags |= F_TIME_STOP;
		return 1;
	case 'l': /* --localtz */
		if (*flags & F_TIMEZONE)
			exit_error(PARAMETER_PROBLEM,
			           "Can only specify exactly one of --localtz or --utc");
		info->flags |= XT_TIME_LOCAL_TZ;
		*flags |= F_TIMEZONE;
		return 1;
	case 'm': /* --monthdays */
		if (*flags & F_MONTHDAYS)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --monthdays more than once");
		info->monthdays_match = time_parse_monthdays(optarg);
		if (invert)
			info->monthdays_match ^= XT_TIME_ALL_MONTHDAYS;
		*flags |= F_MONTHDAYS;
		return 1;
	case 'w': /* --weekdays */
		if (*flags & F_WEEKDAYS)
			exit_error(PARAMETER_PROBLEM,
			           "Cannot specify --weekdays more than once");
		info->weekdays_match = time_parse_weekdays(optarg);
		if (invert)
			info->weekdays_match ^= XT_TIME_ALL_WEEKDAYS;
		*flags |= F_WEEKDAYS;
		return 1;
	case 'u': /* --utc */
		if (*flags & F_TIMEZONE)
			exit_error(PARAMETER_PROBLEM,
			           "Can only specify exactly one of --localtz or --utc");
		info->flags &= ~XT_TIME_LOCAL_TZ;
		*flags |= F_TIMEZONE;
		return 1;
	}
	return 0;
}

static void time_print_date(time_t date, const char *command)
{
	struct tm *t;

	/* If it is the default value, do not print it. */
	if (date == 0 || date == LONG_MAX)
		return;

	t = localtime(&date);
	if (command != NULL)
		/*
		 * Need a contiguous string (no whitespaces), hence using
		 * the ISO 8601 "T" variant.
		 */
		printf("%s %04u-%02u-%02uT%02u:%02u:%02u ",
		       command, t->tm_year + 1900, t->tm_mon + 1,
		       t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	else
		printf("%04u-%02u-%02u %02u:%02u:%02u ",
		       t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
		       t->tm_hour, t->tm_min, t->tm_sec);
}

static void time_print_monthdays(uint32_t mask, bool human_readable)
{
	unsigned int i, nbdays = 0;

	for (i = 1; i <= 31; ++i)
		if (mask & (1 << i)) {
			if (nbdays++ > 0)
				printf(",");
			printf("%u", i);
			if (human_readable)
				switch (i % 10) {
					case 1:
						printf("st");
						break;
					case 2:
						printf("nd");
						break;
					case 3:
						printf("rd");
						break;
					default:
						printf("th");
						break;
				}
		}
	printf(" ");
}

static void time_print_weekdays(unsigned int mask)
{
	unsigned int i, nbdays = 0;

	for (i = 1; i <= 7; ++i)
		if (mask & (1 << i)) {
			if (nbdays > 0)
				printf(",%s", week_days[i]);
			else
				printf("%s", week_days[i]);
			++nbdays;
		}
	printf(" ");
}

static inline void divide_time(unsigned int fulltime, unsigned int *hours,
    unsigned int *minutes, unsigned int *seconds)
{
	*seconds  = fulltime % 60;
	fulltime /= 60;
	*minutes  = fulltime % 60;
	*hours    = fulltime / 60;
}

static void time_print(const void *ip, const struct xt_entry_match *match,
                       int numeric)
{
	struct xt_time_info *info = (void *)match->data;
	unsigned int h, m, s;

	printf("TIME ");

	if (info->daytime_start != XT_TIME_MIN_DAYTIME ||
	    info->daytime_stop != XT_TIME_MAX_DAYTIME) {
		divide_time(info->daytime_start, &h, &m, &s);
		printf("from %02u:%02u:%02u ", h, m, s);
		divide_time(info->daytime_stop, &h, &m, &s);
		printf("to %02u:%02u:%02u ", h, m, s);
	}
	if (info->weekdays_match != XT_TIME_ALL_WEEKDAYS) {
		printf("on ");
		time_print_weekdays(info->weekdays_match);
	}
	if (info->monthdays_match != XT_TIME_ALL_MONTHDAYS) {
		printf("on ");
		time_print_monthdays(info->monthdays_match, true);
	}
	if (info->date_start != 0) {
		printf("starting from ");
		time_print_date(info->date_start, NULL);
	}
	if (info->date_stop != INT_MAX) {
		printf("until date ");
		time_print_date(info->date_stop, NULL);
	}
	if (!(info->flags & XT_TIME_LOCAL_TZ))
		printf("UTC ");
}

static void time_save(const void *ip, const struct xt_entry_match *match)
{
	const struct xt_time_info *info = (const void *)match->data;
	unsigned int h, m, s;

	if (info->daytime_start != XT_TIME_MIN_DAYTIME ||
	    info->daytime_stop != XT_TIME_MAX_DAYTIME) {
		divide_time(info->daytime_start, &h, &m, &s);
		printf("--timestart %02u:%02u:%02u ", h, m, s);
		divide_time(info->daytime_stop, &h, &m, &s);
		printf("--timestop %02u:%02u:%02u ", h, m, s);
	}
	if (info->monthdays_match != XT_TIME_ALL_MONTHDAYS) {
		printf("--monthdays ");
		time_print_monthdays(info->monthdays_match, false);
	}
	if (info->weekdays_match != XT_TIME_ALL_WEEKDAYS) {
		printf("--weekdays ");
		time_print_weekdays(info->weekdays_match);
		printf(" ");
	}
	time_print_date(info->date_start, "--datestart");
	time_print_date(info->date_stop, "--datestop");
	if (!(info->flags & XT_TIME_LOCAL_TZ))
		printf("--utc ");
}

static struct xtables_match time_match = {
	.name          = "time",
	.family        = AF_UNSPEC,
	.version       = XTABLES_VERSION,
	.size          = XT_ALIGN(sizeof(struct xt_time_info)),
	.userspacesize = XT_ALIGN(sizeof(struct xt_time_info)),
	.help          = time_help,
	.init          = time_init,
	.parse         = time_parse,
	.print         = time_print,
	.save          = time_save,
	.extra_opts    = time_opts,
};

void _init(void)
{
	xtables_register_match(&time_match);
}
