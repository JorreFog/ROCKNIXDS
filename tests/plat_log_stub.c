/* The parsers log through plat_log. The tests don't keep a log file. */
void plat_log(const char *fmt, ...)
{
    (void)fmt;
}
