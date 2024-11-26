Value qsearch(Pos *pos, Stack *ss, Value alpha, Value beta, Depth depth,
              int PvNode, int InCheck) {
  assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
  assert(PvNode || (alpha == beta - 1));
  assert(depth <= 0);

  Move pv[MAX_PLY + 1];
  TTEntry *tte;
  Key posKey;
  Move ttMove, move, bestMove;
  Value bestValue, value, ttValue, futilityValue, futilityBase, oldAlpha;
  int ttHit, ttPv, givesCheck, evasionPrunable;
  Depth ttDepth;
  int moveCount;

  if (PvNode) {
    oldAlpha = alpha; // To flag BOUND_EXACT when eval above alpha and no
                      // available moves
    (ss + 1)->pv = pv;
    ss->pv[0] = 0;
  }

  bestMove = 0;
  moveCount = 0;

  // Check for an instant draw or if the maximum ply has been reached
  if (is_draw(pos) || ss->ply >= MAX_PLY) {
    return ss->ply >= MAX_PLY && !InCheck ? evaluate(pos) : VALUE_DRAW;
  }

  assert(0 <= ss->ply && ss->ply < MAX_PLY);

  // Determine TT depth
  ttDepth = InCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS
                                                : DEPTH_QS_NO_CHECKS;

  // Transposition table lookup
  posKey = key();
  tte = tt_probe(posKey, &ttHit);
  ttValue = ttHit ? value_from_tt(tte_value(tte), ss->ply, rule50_count())
                  : VALUE_NONE;
  ttMove = ttHit ? tte_move(tte) : 0;
  ttPv = ttHit ? tte_is_pv(tte) : 0;

  if (!PvNode && ttHit && tte_depth(tte) >= ttDepth &&
      ttValue != VALUE_NONE // Only in case of TT access race
      && (ttValue >= beta ? (tte_bound(tte) & BOUND_LOWER)
                          : (tte_bound(tte) & BOUND_UPPER))) {
    return ttValue;
  }

  // Static evaluation
  if (InCheck) {
    ss->staticEval = VALUE_NONE;
    bestValue = futilityBase = -VALUE_INFINITE;
  } else {
    if (ttHit) {
      if ((ss->staticEval = bestValue = tte_eval(tte)) == VALUE_NONE) {
        ss->staticEval = bestValue = evaluate(pos);
      }

      if (ttValue != VALUE_NONE &&
          (tte_bound(tte) &
           (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER))) {
        bestValue = ttValue;
      }
    } else {
      ss->staticEval = bestValue = (ss - 1)->currentMove != MOVE_NULL
                                       ? evaluate(pos)
                                       : -(ss - 1)->staticEval + 2 * Tempo;
    }

    if (bestValue >= beta) {
      if (!ttHit) {
        tte_save(tte, posKey, value_to_tt(bestValue, ss->ply), ttPv,
                 BOUND_LOWER, DEPTH_NONE, 0, ss->staticEval, tt_generation());
      }
      return bestValue;
    }

    if (PvNode && bestValue > alpha) {
      alpha = bestValue;
    }

    futilityBase = bestValue + 154;
  }

  ss->history = &(*pos->counterMoveHistory)[0][0];

  mp_init_q(pos, ttMove, depth, to_sq((ss - 1)->currentMove));

  while ((move = next_move(pos, 0))) {
    assert(move_is_ok(move));

    givesCheck = gives_check(pos, ss, move);
    moveCount++;

    if (!InCheck && !givesCheck && futilityBase > -VALUE_KNOWN_WIN &&
        !advanced_pawn_push(pos, move)) {
      futilityValue = futilityBase + PieceValue[EG][piece_on(to_sq(move))];

      if (futilityValue <= alpha) {
        bestValue = max(bestValue, futilityValue);
        continue;
      }

      if (futilityBase <= alpha && !see_test(pos, move, 1)) {
        bestValue = max(bestValue, futilityBase);
        continue;
      }
    }

    evasionPrunable = InCheck && (depth != 0 || moveCount > 2) &&
                      bestValue > VALUE_MATED_IN_MAX_PLY &&
                      !is_capture(pos, move);

    if ((!InCheck || evasionPrunable) && !see_test(pos, move, 0)) {
      continue;
    }

    prefetch(tt_first_entry(key_after(pos, move)));

    if (!is_legal(pos, move)) {
      moveCount--;
      continue;
    }

    ss->currentMove = move;
    ss->history = &(*pos->counterMoveHistory)[moved_piece(move)][to_sq(move)];

    do_move(pos, move, givesCheck);
    value = -qsearch(pos, ss + 1, -beta, -alpha, depth - 1, PvNode, givesCheck);
    undo_move(pos, move);

    assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

    if (value > bestValue) {
      bestValue = value;

      if (value > alpha) {
        bestMove = move;

        if (PvNode) {
          update_pv(ss->pv, move, (ss + 1)->pv);
        }

        if (PvNode && value < beta) {
          alpha = value;
        } else {
          break;
        }
      }
    }
  }

  if (InCheck && bestValue == -VALUE_INFINITE) {
    return mated_in(ss->ply);
  }

  tte_save(tte, posKey, value_to_tt(bestValue, ss->ply), ttPv,
           bestValue >= beta                  ? BOUND_LOWER
           : (PvNode && bestValue > oldAlpha) ? BOUND_EXACT
                                              : BOUND_UPPER,
           ttDepth, bestMove, ss->staticEval, tt_generation());

  assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

  return bestValue;
}