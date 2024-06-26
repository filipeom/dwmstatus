#define _BSD_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <mpd/client.h>

/* Icons */
static char *sep = "\ue0bb";
static char *bolt = "\uf0e7";
static char *cpu = "\uf4bc\u2009";
static char *clk = "\uf017";
static char *cal = "\uf133\u2009";

static char *tzlisbon = "Europe/Lisbon";

static Display *dpy;

void die(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

void *smalloc(size_t len) {
  void *ptr;

  if (NULL == (ptr = malloc(len))) {
    die("xmalloc: %s\n", strerror(errno));
  }

  return ptr;
}

char *smprintf(char *fmt, ...) {
  int len = 0;
  char *p = NULL;
  va_list ap;

  va_start(ap, fmt);
  len = vsnprintf(p, len, fmt, ap);
  va_end(ap);

  p = smalloc(++len);

  va_start(ap, fmt);
  vsnprintf(p, len, fmt, ap);
  va_end(ap);

  return p;
}

void set_status(char *str) {
  XStoreName(dpy, DefaultRootWindow(dpy), str);
  XSync(dpy, False);
}

void set_tz(char *tzname) { setenv("TZ", tzname, 1); }

char *mk_times(char *fmt, char *tzname) {
  char buf[129];
  time_t tim;
  struct tm *timtm;

  set_tz(tzname);
  tim = time(NULL);
  timtm = localtime(&tim);
  if (NULL == timtm)
    return smprintf("");

  if (0 == strftime(buf, sizeof(buf) - 1, fmt, timtm))
    return smprintf("");

  return smprintf("%s", buf);
}

char *read_file(char *base, char *file) {
  char *path, line[513];
  FILE *fd;

  memset(line, 0, sizeof(line));

  path = smprintf("%s/%s", base, file);
  fd = fopen(path, "r");
  free(path);
  if (fd == NULL)
    return NULL;

  if (NULL == fgets(line, sizeof(line) - 1, fd))
    return NULL;
  fclose(fd);

  return smprintf("%s", line);
}

char *get_battery(char *base) {
  int cap;
  char *co, status;

  cap = -1;

  co = read_file(base, "present");
  if (co == NULL)
    return smprintf("");
  if (co[0] != '1') {
    free(co);
    return smprintf("not present");
  }
  free(co);

  co = read_file(base, "capacity");
  if (co == NULL)
    return smprintf("");
  sscanf(co, "%d", &cap);
  free(co);

  co = read_file(base, "status");
  if (!strncmp(co, "Discharging", 11)) {
    status = '-';
  } else if (!strncmp(co, "Charging", 8)) {
    status = '+';
  } else if (!strncmp(co, "Full", 4)) {
    status = -1;
  } else {
    status = '?';
  }
  free(co);

  if (cap < 0)
    return smprintf("invalid");

  if (-1 == status)
    return smprintf("%d%%", cap);
  else
    return smprintf("%d%% (%c)", cap, status);
}

char *get_temperature(char *base, char *sensor) {
  char *co;

  co = read_file(base, sensor);
  if (co == NULL)
    return smprintf("");
  return smprintf("%02.0f°C", atof(co) / 1000);
}

char *get_mpd() {
  char *mpdstat;
  int elapsed, total;
  struct mpd_song *song;
  const char *title = NULL;
  const char *artist = NULL;
  struct mpd_connection *conn;

  elapsed = total = 0;
  song = NULL, mpdstat = NULL;

  conn = mpd_connection_new("localhost", 0, 6600);
  if ((NULL == conn) || mpd_connection_get_error(conn))
    return smprintf("");

  mpd_command_list_begin(conn, true);
  mpd_send_status(conn);
  mpd_send_current_song(conn);
  mpd_command_list_end(conn);

  struct mpd_status *status = mpd_recv_status(conn);
  if ((NULL != status) && (MPD_STATE_PLAY == mpd_status_get_state(status))) {
    mpd_response_next(conn);
    song = mpd_recv_song(conn);
    title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
    artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
    elapsed = mpd_status_get_elapsed_time(status);
    total = mpd_status_get_total_time(status);

    mpdstat = smprintf("%.2d:%.2d/%.2d:%.2d %s - %s", elapsed / 60,
                       elapsed % 60, total / 60, total % 60, artist, title);
    mpd_song_free(song);
  } else {
    mpdstat = smprintf("");
  }

  mpd_response_finish(conn);
  mpd_connection_free(conn);

  return mpdstat;
}

int main(void) {
  char *bat;
  char *st;
  char *date;
  char *time;
  char *temp;

  if (!(dpy = XOpenDisplay(NULL))) {
    fprintf(stderr, "dwmstatus: cannot open display.\n");
    return 1;
  }

  for (;; sleep(5)) {
    time = mk_times("%R", tzlisbon);
    date = mk_times("%a %d %b", tzlisbon);
    bat = get_battery("/sys/class/power_supply/BAT0");
    temp = get_temperature("/sys/class/thermal", "thermal_zone8/temp");

    /* status = smprintf(" %s | %s | %s", temperature, battery, datetime); */
    st = smprintf(" %s %s %s %s %s %s %s %s %s %s %s %s", sep, cpu, temp, sep,
                  bolt, bat, sep, cal, date, sep, clk, time);
    set_status(st);

    free(st);
    free(temp);
    free(date);
    free(bat);
  }

  XCloseDisplay(dpy);

  return 0;
}
