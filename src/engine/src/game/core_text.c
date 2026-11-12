/*
 * String tables (strings.bin, resource 1002).
 *
 * strings.bin: int count; int groupOfs[count]; then NUL-separated strings.
 * A group's text runs from 4 + groupOfs[g] to 4 + groupOfs[g+1]. A loaded
 * group lives in Canvas.strData[g] (bytes), strOffsets[g] (start of each
 * string), strCount[g] and strDataSize[g].
 */
#include "core_local.h"

#define RES_STRINGS 1002

static void add_mem_stat(int32_t *pair, int32_t v) {
  /* Render.memStats are int64 {lo, hi} pairs */
  int64_t cur =
      (int64_t)(((uint64_t)(uint32_t)pair[1] << 32) | (uint32_t)pair[0]);
  cur += v;
  pair[0] = (int32_t)(uint32_t)cur;
  pair[1] = (int32_t)(uint32_t)((uint64_t)cur >> 32);
}

/* 0x00029990  Load string group `group` into the canvas tables. */
int Text_loadStringGroup(Canvas *canvas, int group) {
  Applet *app = g_app;
  InputStream res;
  IntArray groups;
  int ok = 0;

  Canvas_unloadStringGroup(canvas, group);
  InputStream_ctor(&res);
  if (Resource_open(&res, RES_STRINGS, 1)) {
    int start, n, count, k;
    char *text;
    int16_t *offsets;

    Stream_readIntArray(app->dataBuffer, &res, &groups);
    start = groups.data[group];
    n = groups.data[group + 1] - start;
    /* one spare NUL byte so reading the (out of range) last offset is safe */
    text = calloc((size_t)n + 1, 1);
    InputStream_skipChunked(app->dataBuffer, &res, start - groups.count * 4, 0);
    if (text && Stream_read(&res, text, 0, n) == n) {
      canvas->strDataSize[group] = n;
      canvas->strData[group] = text;
      InputStream_close(&res);

      count = 1;
      for (int i = 0; i < canvas->strDataSize[group]; i++)
        if (canvas->strData[group][i] == '\0')
          count++;
      /* count entries as in the original, plus one spare */
      offsets = calloc((size_t)count + 1, sizeof(int16_t));
      canvas->strOffsets[group] = offsets;
      canvas->strCount[group] = count;
      offsets[0] = 0;
      k = 1;
      for (int i = 0; i < n; i++)
        if (canvas->strData[group][i] == '\0')
          offsets[k++] = (int16_t)(i + 1);
      offsets[count] = (int16_t)n;

      add_mem_stat(&app->render->memStats[2],
                   canvas->strDataSize[group] + canvas->strCount[group] * 2);
      ok = 1;
    } else {
      free(text);
    }
    Array_dtor(&groups, 0);
  }
  InputStream_dtor(&res, 0);
  return ok;
}

/* 0x0000a2d8  Append string idx of a loaded group to `out`, expanding
 * "%1".."%6" with Canvas.textArgs (or textIntArgs when the text arg is empty),
 * "\%" to '%' and "\n" to a newline. */
void Text_getString(Canvas *canvas, int group, int idx, String *out) {
#ifdef OE2_STANDALONE_BRIDGE
  if (group == 3 && idx == 0x47) {
    String_appendCStr_76f0(out, "Help");
    return;
  }
  if (group == 0) {
    switch (idx) {
    case 0:
      String_appendCStr_76f0(
          out, "TALK\nPress CROSS to talk to characters with chat bubbles.");
      return;
    case 2:
      String_appendCStr_76f0(
          out, "LOCK PICKING\nPress each D--Pad direction until you discover "
               "which tumbler fits the lock. Once you find it, press the same "
               "direction to set the tumbler.");
      return;
    case 6:
      String_appendCStr_76f0(out,
                             "NOTE\nNote added to your Quest Log. Open menu "
                             "(START) then select Quest Log to review notes.");
      return;
    case 8:
      String_appendCStr_76f0(out,
                             "DOOR\nPress CROSS to open a door. Some doors "
                             "must be unlocked before you can open them.");
      return;
    case 15:
      String_appendCStr_76f0(out,
                             "WEAPON SELECTION\nUse TRIANGLE to cycle weapons, "
                             "or press START then select \"Inventory\".");
      return;
    case 34:
      String_appendCStr_76f0(out,
                             "To use, press START then select \"Inventory\".");
      return;
    case 35:
      String_appendCStr_76f0(
          out, "To use, press START then select \"Inventory\" then \"Other\".");
      return;
    case 36:
      String_appendCStr_76f0(out, "Use TRIANGLE to cycle weapons, or press "
                                  "START then select \"Inventory\".");
      return;
    case 37:
      String_appendCStr_76f0(out,
                             "To wear, press START then select \"Inventory\".");
      return;
    case 0xaa:
      String_appendCStr_76f0(out, "Press CROSS");
      return;
    case 195:
      String_appendCStr_76f0(out, "Health restored.\nTo see your stats press "
                                  "START then select \"Status\".");
      return;
    }
  } else if ((group == 6 && idx == 107) || (group == 10 && idx == 52)) {
    String_appendCStr_76f0(
        out, "REMINDER\nSave your game progress at any time by opening the "
             "menu (START) and selecting \"Save Game\".");
    return;
  }
#endif
  const char *text = canvas->strData[group];
  int16_t pos;
  char c;

  if (idx < 0 || idx > canvas->strCount[group] || text == NULL)
    return;
  pos = canvas->strOffsets[group][idx];
  for (c = text[pos]; c != '\0'; pos = (int16_t)(pos + 1), c = text[pos]) {
    if (c == '\\') {
      char e;
      pos = (int16_t)(pos + 1);
      e = text[pos];
      if (e == '%') {
        String_appendChar(out, '%');
      } else if (e == 'n') {
        String_appendChar(out, '\n');
      } else {
        String_appendChar(out, '\\');
        String_appendChar(out, (uint8_t)e);
      }
    } else if (c == '%') {
      int arg;
      pos = (int16_t)(pos + 1);
      arg = (uint8_t)text[pos] - '1';
      if (arg < 0 || arg >= canvas->numTextArgs)
        return;
      if (String_length(&canvas->textArgs[arg]) > 0)
        String_appendString(out, &canvas->textArgs[arg]);
      else
        String_appendInt(out, canvas->textIntArgs[arg]);
    } else {
      String_appendChar(out, (uint8_t)c);
    }
  }
}

/* 0x00019fd8  Index of the string equal to `str` in a loaded group, or -1. */
int Text_findString(Canvas *canvas, int group, String *str) {
  const char *text = canvas->strData[group];
  const char *want = String_cstr(str);
  int16_t i = 0;

  /* (the original also compared str with an empty String and ignored the
   * result) */
  while (i < canvas->strCount[group]) {
    int16_t start = canvas->strOffsets[group][i];
    int16_t pos = start;
    int matched = 0;
    i = (int16_t)(i + 1);
    while (matched <= String_length(str) &&
           (uint8_t)text[pos] == (uint8_t)want[matched]) {
      pos = (int16_t)(pos + 1);
      matched = pos - start;
      if (String_length(str) == matched) {
        if (text[pos] == '\0')
          return (int16_t)(i - 1);
        break;
      }
    }
  }
  return -1;
}

/* 0x00025da4  Append string idx of group `group` straight from strings.bin
 * without loading the group (used for long help texts). */
void Text_readStringFromRes(Canvas *canvas, int group, int idx,
                            String *outStr) {
#ifdef OE2_STANDALONE_BRIDGE
  if (group == 2 /* TEXT_GROUP_HELP */) {
    switch (idx) {
    case 1: /* How to Play */
      String_appendCStr_76f0(
          outStr,
          "@HOW TO PLAY|"
          "Use the Left Stick or D--Pad to move, turn, and navigate menus. "
          "Press CROSS to talk, attack, and perform other actions.||"
          "@CONTROLS|"
          "CROSS attack/talk/use|"
          "SQUARE pass turn/skip|"
          "SELECT view automap|"
          "START open menu/pause|"
          "CIRCLE back/cancel|"
          "TRIANGLE next screen");
      return;
    case 2: /* How to Move */
      String_appendCStr_76f0(outStr, "@HOW TO MOVE|"
                                     "LEFT STICK / D--PAD:|"
                                     "UP    move forward|"
                                     "DOWN  move back|"
                                     "LEFT  turn left|"
                                     "RIGHT turn right||"
                                     "L / R TRIGGERS:|"
                                     "L TRIGGER  strafe left|"
                                     "R TRIGGER  strafe right");
      return;
    case 3: /* How to Attack */
      String_appendCStr_76f0(
          outStr, "@HOW TO ATTACK|"
                  "CROSS attack/talk/use|"
                  "TRIANGLE next weapon|"
                  "L+TRIANGLE prev weapon|"
                  "SQUARE pass turn/skip||"
                  "Hold L and press SQUARE for quick weapon selection.");
      return;
    case 7: /* Lock Picking */
      String_appendCStr_76f0(
          outStr,
          "@LOCK PICKING|"
          "To initiate lock pick mode, face lock and press CROSS. Press UP, "
          "DOWN, LEFT, or RIGHT on the D--Pad to start setting the current "
          "tumbler. Press the same direction again to finish setting the "
          "tumbler. Continue till all tumblers are set.||"
          "@LOCKED OBJECTS|"
          "Throughout the game you will find locked treasure chests and doors "
          "that can be unlocked with your lock picks, a key, or a lever.");
      return;
    }
  }
#endif
  Applet *app = g_app;
  InputStream res;
  IntArray groups;
  (void)canvas;

  InputStream_ctor(&res);
  if (Resource_open(&res, RES_STRINGS, 1)) {
    int inString = 0, cur = 0, n, done = 0;
    Stream_readIntArray(app->dataBuffer, &res, &groups);
    InputStream_skipChunked(app->dataBuffer, &res,
                            groups.data[group] - groups.count * 4, 0);
    while (!done &&
           (n = Stream_read(&res, app->dataBuffer->data, 0, 0x4000)) > 0) {
      for (int i = 0; i < n; i++) {
        uint8_t c = app->dataBuffer->data[i];
        if (inString || cur == idx) {
          if (c == '\0') {
            done = 1;
            break;
          }
          inString = 1;
          String_appendChar(outStr, c);
        } else if (c == '\0') {
          cur++;
        }
      }
    }
    if (!done)
      InputStream_close(&res);
    Array_dtor(&groups, 0);
  }
  InputStream_dtor(&res, 0);
}
