/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "misc.h"
#include "position.h"
#include "search.h"
#include "settings.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

static char *Defaults[] = {
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
};

// benchmark() runs a simple benchmark by letting Stockfish analyze a set
// of positions for a given limit each. There are five parameters: the
// transposition table size, the number of search threads that should
// be used, the limit value spent for each position (optional, default is
// depth 13), an optional file name where to look for positions in FEN
// format (defaults are the positions defined above) and the type of the
// limit value: depth (default), time in millisecs or number of nodes.

void benchmark(Pos *current, char *str)
{
  char *token;
  char **fens;
  size_t num_fens;

  Limits.time[0] = Limits.time[1] = Limits.inc[0] = Limits.inc[1] = 0;
  Limits.npmsec = Limits.movestogo = Limits.depth = Limits.movetime = 0;
  Limits.mate = Limits.infinite = Limits.ponder = Limits.num_searchmoves = 0;
  Limits.nodes = 0;

  int ttSize = 16, threads = 1;
  int64_t limit = 13;
  char *fenFile = NULL, *limitType = "";

  token = strtok(str, " ");
  if (token) {
    ttSize = atoi(token);
    token = strtok(NULL, " ");
    if (token) {
      threads = atoi(token);
      token = strtok(NULL, " ");
      if (token) {
        limit = atoll(token);
        fenFile = strtok(NULL, " ");
        if (fenFile) {
          token = strtok(NULL, " ");
          if (token) limitType = token;
        }
      }
    }
  }

  delayed_settings.tt_size = ttSize;
  delayed_settings.num_threads = threads;
  process_delayed_settings();
  search_clear();

  if (strcmp(limitType, "time") == 0)
    Limits.movetime = limit; // movetime is in millisecs
  else if (strcmp(limitType, "nodes") == 0)
    Limits.nodes = limit;
  else if (strcmp(limitType, "mate") == 0)
    Limits.mate = limit;
  else
    Limits.depth = limit;

  if (!fenFile || strcmp(fenFile, "default") == 0) {
    fens = Defaults;
    num_fens = sizeof(Defaults) / sizeof(char *);
  }
  else if (strcmp(fenFile, "current") == 0) {
    fens = malloc(sizeof(char *));
    fens[0] = malloc(128);
    pos_fen(current, fens[0]);
    num_fens = 1;
  }
  else {
    size_t max_fens = 100;
    num_fens = 0;
    FILE *F = fopen(fenFile, "r");
    if (!F) {
      fprintf(stderr, "Unable to open file %s\n", fenFile);
      return;
    }
    fens = malloc(max_fens * sizeof(char *));
    fens[0] = NULL;
    size_t length = 0;
    while (getline(&fens[num_fens], &length, F) > 0) {
      num_fens++;
      if (num_fens == max_fens) {
        max_fens += 100;
        fens = realloc(fens, max_fens * sizeof(char *));
      }
      fens[num_fens] = NULL;
      length = 0;
    }
    fclose(F);
  }

  uint64_t nodes = 0;
  Pos pos;
  pos.stack = malloc(215 * sizeof(Stack));
  pos.st = pos.stack + 5;
  pos.moveList = malloc(10000 * sizeof(ExtMove));
  TimePoint elapsed = now();

  int num_opts = 0;
  for (size_t i = 0; i < num_fens; i++)
    if (strncmp(fens[i], "setoption ", 9) == 0)
      num_opts++;

  for (size_t i = 0, j = 0; i < num_fens; i++) {
    char buf[128];

    if (strncmp(fens[i], "setoption ", 9) == 0) {
      strncpy(buf, fens[i] + 10, 127 - 10);
      buf[127] = 0;
      setoption(buf);
      continue;
    }

    strcpy(buf, "fen ");
    strncat(buf, fens[i], 127 - 4);
    buf[127] = 0;

    position(&pos, buf);

    fprintf(stderr, "\nPosition: %" FMT_Z "u/%" FMT_Z "u\n", ++j, num_fens - num_opts);

    Limits.startTime = now();
    start_thinking(&pos);
    thread_wait_for_search_finished(threads_main());
    nodes += threads_nodes_searched();
  }

  elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

  fprintf(stderr, "\n==========================="
                  "\nTotal time (ms) : %" PRIu64
                  "\nNodes searched  : %" PRIu64
                  "\nNodes/second    : %" PRIu64 "\n",
                  elapsed, nodes, 1000 * nodes / elapsed);

  if (fens != Defaults) {
    for (size_t i = 0; i < num_fens; i++)
      free(fens[i]);
    free(fens);
  }
  free(pos.stack);
  free(pos.moveList);
}

