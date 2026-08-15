// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// search_state.h -- Search configuration, per-search mutable state, and cancellation.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_SEARCH_STATE_H_INCLUDED
#define MATEPROVER_SEARCH_STATE_H_INCLUDED

namespace mateprover {

// Where parallel split points are published and claimed. Defined in prove.h,
// because the node that publishes them is prove_attacker's move loop and the
// whole point of this design is that there is no second copy of that loop.
struct SplitRegistry;

// All tunable search options. This is the single value passed from the CLI to
// the search, and is also what each worker copies when a search is split
// across threads, so worker construction never has to re-parse or re-plumb
// individual flags.
struct SearchConfig {
    Color attacker = WHITE;
    // DFPN is the default route.
    //
    // It was rejected in 8e as slower at every depth, which was measuring a
    // defect: its transposition key omitted the remaining depth, so every depth
    // shared one entry and the proof numbers were meaningless (21). Two further
    // fixes followed -- preconditioning only the deepest iteration (23) and
    // dropping the per-child work that computed a value already known (27),
    // which cut make_move calls per DFPN node from 24.3 to 4.34.
    //
    // Measured once on freshly minted evaluation sets, in the shipped
    // configuration: at mate-in-10, 54/60 against depth-first's 37/60, gaining
    // seventeen positions and losing none. At mate-in-8, 171/200 against
    // 167/200 -- a slight gain that is not significant on its own, but it is a
    // gain, so the deep result is not bought at the shallow one's expense.
    //
    // Still a preconditioner, never an output authority: every accepted proof,
    // PV and certificate comes from the exact prover.
    RouteKind route = RouteKind::Dfpn;
    // Directmate unless asked otherwise. See types.h: the goals are disjoint,
    // so nothing computed for one is valid for the other, and the goal is part
    // of the transposition key for exactly that reason.
    Goal goal = Goal::Mate;
    bool debug = false;
    bool emit_proof = false;
    // Any-depth refutations (GAP-1). DEFAULT OFF, and off means inert rather
    // than merely unused: `Refuted` enters the lattice only through the gated
    // axioms below, and every composition rule needs a `Refuted` child, so with
    // this false no node can ever become `Refuted` and the search is
    // byte-identical to the version without it. See docs/GAP1_DERIVATION.md §7.
    bool any_depth_refutations = false;
    // The helpmate reachability/coverage bound. On by default. The switch exists
    // so the admissibility gate can be RUN, not because anyone should turn it
    // off: a bound that can only lose solutions silently has to be differentially
    // testable, and 74 shipped an unsound one because it was not.
    bool help_bound = true;
    // The same bound applied to selfmate, where the DEFENDER mates.
    //
    // DEFAULT OFF, on measurement rather than on doubt. It is SOUND -- the five
    // positions it appeared to lose over the 903 all solve with it enabled given
    // 60 s, so none was a false prune -- but at a 5 s cap it is a net loss of
    // three: 570 against 573. It fires tens of millions of times per position
    // and converts nothing, because in a selfmate the mated side is the ATTACKER,
    // who cooperates in being mated and self-blocks his own king's flights. That
    // makes the handled set enormous and the prune rare, while the test still
    // costs at every node. See docs/SELFMATE_REACH_DERIVATION.md and section 77.
    bool selfmate_bound = false;
    // Order the selfmate defender's replies by how little room they leave the
    // attacker. On by default; the off switch is the differential test.
    // The exact proof table. On always, except for the equivalence test that
    // is the only way to check the depthless key's preconditions.
    bool exact_tt = true;
    bool answer_order = true;
    // Use the additive scorer at remaining depth 2 rather than the width
    // estimator. Off until measured.
    bool depth2_scorer = false;
    // Measurement only: count how many depth-1 attacker moves the selfmate
    // rejection test WOULD discard, without acting on it.
    bool reject_observer = false;
    // The node-level mate-in-one disproof. On by default: it is exact, so it
    // cannot change a verdict, and the off switch exists to prove that.
    bool coverage_exit = true;
    bool coverage_observer = false;
    // The selfmate counterpart of the coverage exit. Default off until it is
    // measured; the observer answers Q1 and Q2 before the mechanism is trusted.
    bool selfmate_node_exit = false;
    bool selfmate_node_observer = false;
    // Answer the rejection test with attack queries instead of board copies,
    // deferring to the reference whenever the cheap form cannot be certain.
    bool fast_reject = true;
    // Measures what a fatal-anti-check test could still save, given the reply
    // ordering already in place. A measurement aid, not a tuning knob.
    bool fac_observer = false;
    // Variant quotas the CLI supplies, indexed colour * VR_COUNT + rule; -1
    // leaves that rule out of force, and a fifth Forsyth field on the input line
    // overrides the lot. `rule_wins` decides what reaching a quota MEANS under a
    // mate goal: a win in the variant's own terms, or -- switched off -- nothing,
    // for the problemist who stipulated checkmate.
    std::array<int, kQuotaSlots> quota_limit{{-1, -1, -1, -1, -1, -1}};
    std::array<bool, VR_COUNT> rule_wins{{true, true, true}};
    // Act on the rejection test rather than merely counting it.
    bool attacker_reject = true;
    // Remaining depth at or above which replies are ordered. Below it the
    // lazy scan runs instead, since there is no subtree left to shape and
    // materialising the list to sort it would be pure cost.
    int answer_order_min_depth = 2;
    // A candidate pruning predicate, evaluated and MEASURED but never acted
    // on. See predicate.h for why falsification can be automated and promotion
    // cannot. Inert unless --predicate is given.
    Predicate predicate;
    // Move-ordering weights. Soundness-neutral by construction -- see
    // OrderWeights -- which is what makes them safe to hand to tools/autotune.py.
    OrderWeights order_weights;
    bool score_mates = false;
    bool score_checks = true;
    bool fast_check_score = false;
    bool refutation_hints = false;
    bool proof_hints = true;
    std::size_t tt_reserve = 0;
    bool move_reserve = true;
    std::size_t move_reserve_capacity = 96;
    bool inplace_order = true;
    bool static_pseudo = false;
    bool profile = false;
    std::size_t order_min_size = 2;
    bool bucket_order = false;
    bool keep_iter_tt = true;
    bool ordered_check_shortcut = true;
    // -1 means the user did not choose: main() resolves it to the same value
    // `--threads auto` computes. An explicit --threads N, including 1, is
    // honoured exactly.
    int threads = -1;
    bool fused_order = true;
    bool lazy_defender = false;
    // DFPN preconditioner. Disproofs it establishes are exact verdicts and are
    // shared with the exact table; proofs are shared only as ordering hints.
    bool dfpn_share_disproofs = true;
    std::uint64_t dfpn_node_limit = 0; // 0 = unlimited
    // Root moves searched sequentially before the split. 0 splits everything.
    int root_sequential_first = 0;

    // Split the root across workers on the DFPN route. OFF by default.
    //
    // The split engages correctly -- node counts move with the thread count,
    // which they never did before -- but the suite's sequential-agreement check
    // finds a case where the parallel answer differs from the single-threaded
    // one, and an answer that depends on the thread count is not an answer this
    // engine is allowed to give. Opt in with --root-split; the default stays
    // bit-identical to the sequential search until that divergence is explained.
    // ON by default, on a measurement rather than on principle.
    //
    // It was defaulted off when it appeared to buy nothing and to change the
    // reported line; 32a explains why it appeared to buy nothing (it was never
    // wired into the default route, and then it was hidden behind a sequential
    // preconditioner). With both fixed it is worth 4.75x at 24 threads on a
    // depth-7 capture-quota search, with the node count moving 2% -- so the
    // workers are sharing rather than duplicating.
    //
    // The cost is that the PV becomes a function of the position AND the thread
    // count, because a worker can walk into a subtree a sibling proved. The key
    // move and the distance are unaffected and every certificate still verifies;
    // see 100. `--no-root-split` restores a PV that depends on the position
    // alone, which is what a caller diffing output against a stored expectation
    // wants.
    bool root_split = true;
    // The SECOND ply of the split: share out one root move's defender replies.
    //
    // DEFAULT OFF, on measurement, and the measurement refuted the argument
    // that motivated the mechanism rather than merely its tuning.
    //
    // The argument: splitting the root alone saturates at 4.75x, and Amdahl
    // says why -- one root move owns about a fifth of the tree, 1/0.21 = 4.76,
    // and at the tail one worker grinds on that move while the rest have no
    // index left to claim. Give them the move's defender REPLIES. A defender
    // node is a conjunction, the attacker needs the goal after every reply, so
    // helpers there are not speculative.
    //
    // The flaw: that conjunction only has to be COMPLETED when the node ends up
    // proved. A node that ends up refuted early-exits at its first refuting
    // reply -- which reply ordering and the refutation-hint table between them
    // make the first reply most of the time -- so it costs one subtree
    // sequentially and helpers charge in to prove twenty more that sequential
    // never looks at. On a no-solution position EVERY node is refuted, and a
    // no-solution position is precisely the workload the 4.75x was measured on.
    //
    // Measured, 24 threads: depth-7 capture quota 7.08 s -> 31.7 s and 21.1M
    // nodes -> 57.1M; twelve mate-in-10s 141.6 s -> 153.2 s and one fewer
    // solved. Gating helpers behind `reply_split_min_proved` removes the
    // damage exactly by never firing -- 120 replies claimed, 0 by a helper --
    // which is the finding stated as a counter.
    //
    // The mechanism is sound and stays: it composes its results in reply-index
    // order, so the verdict, the line and the certificate are bit-identical to
    // the sequential loop's, and the suite checks that. What is wrong with it
    // is the ply it splits, not the way it splits. See architecture 111.
    bool reply_split = false;
    // How many replies a defender node must have PROVED before helpers may
    // join it. The gate that decides whether the mechanism above pays; see
    // claim_any_reply and architecture 111.
    int reply_split_min_proved = 2;

    // YOUNG BROTHERS WAIT, on the OR nodes below the root.
    //
    // 111 established which node type is safe to split, and it is not the one
    // the reply split chose. To DISPROVE a position every attacker move must be
    // refuted, so an OR node in a failing search visits all its children and
    // parallel work there is not speculative. To PROVE one it stops at the first
    // move that works -- so the eldest child is searched alone first, and only
    // if it fails to settle the node do the younger brothers go out to the other
    // workers. That is the classical arrangement and it is classical for this
    // reason.
    //
    // The node this reaches is the one 109 identified and could not touch: the
    // root split leaves one worker grinding on the single most expensive root
    // move, whose cost is really the cost of ONE defender reply, whose cost is
    // really the exhaustive refutation of every attacker move below it. That
    // last list is what gets shared out.
    //
    // Implemented inside prove_attacker rather than beside it. prove_attacker is
    // 250 lines of coverage exits, restrictions, GAP-1 axioms and fail_depth
    // composition, and a faithful second copy of it is not a thing anyone should
    // maintain -- 111's mechanism is a copy of a 60-line loop and even that
    // needed a byte-for-byte differential test to trust.
    bool or_split = true;
    // Plies below the root iteration depth at which an OR node may split.
    //
    // Was 1, on a measurement that had the wrong cause attached to it. Splitting
    // deeper measured worse -- 6.25 s at one ply, 7.07 at two -- and that was
    // read as coordination cost. It was not. A fixed ply publishes whether or
    // not anyone is free to help, so two plies created a split point inside
    // every ply-3 task, twenty of them against a handful of idle workers, and
    // most of the pool became owners blocked at their own tails.
    //
    // With demand gating the depth limit is not what should be doing this job,
    // so it is opened up and `split_is_wanted` decides instead. Kept as a knob
    // because it is the only way to reproduce the measurement above.
    int or_split_plies = 99;
    // Remaining depth below which a node is not worth the coordination.
    //
    // ABSOLUTE, not relative to the root, and that is the point. The cost of
    // publishing a node is fixed; the benefit scales with how much work is under
    // it, which scales with remaining depth in its own right and not with how
    // far down the tree the node happens to sit. A relative rule would split
    // trivial nodes in a deep search and refuse worthwhile ones in a shallow.
    //
    // 4, measured on the depth-7 capture quota at 24 threads, best of two:
    //
    //     not splitting   7.15 s
    //     floor 2         6.80 s
    //     floor 4         5.94 s      <- three levels of OR node
    //     floor 5         6.02 s
    //     floor 6         6.59 s      <- one level, which is 112's fixed ply
    //
    // Splitting deeper than one level is worth 10% once the floor keeps it away
    // from nodes too small to pay for themselves. 112 concluded the opposite
    // from `--or-split-plies 2`, and was wrong twice over: that arm published
    // without demand, and it had no floor.
    int or_split_min_depth = 4;
    // Children searched alone before the rest are shared out. This is the "wait"
    // in young brothers wait; 0 turns it off and splits from the first child.
    //
    // 4, and the 4 is measured. An OR node that is going to SUCCEED stops at its
    // first working move, so sharing the rest out is speculation -- the same
    // mistake the reply split made at the other node type (111), reached from
    // the other side. One eldest child is not enough of a wait: over twelve
    // mate-in-10s, where nearly every node succeeds, `1` cost 3.5% against not
    // splitting at all (138.6/138.7/140.0 s against 133.7/134.1/134.8) and `4`
    // gave it all back (133.3 s). On the disproof workload the wait costs
    // nothing measurable, because a node that never succeeds has no early exit
    // to protect: 6.79 s at 1, 6.82 s at 4.
    //
    // So the wait is not a compromise between the two workloads. It is free on
    // the one it does not help and worth 3.5% on the one it does.
    int ybw_first = 4;
    // df-pn 1+epsilon: widen the proof threshold handed to a child so it keeps
    // working instead of bouncing straight back. Expressed in 1/64ths.
    // Precondition only the deepest iteration.
    //
    // Under iterative deepening the route runs every depth from 1, and DFPN
    // cannot carry work across depths because depth is part of its key (21, 22).
    // Every shallow pass is therefore paid for in full and thrown away, while
    // the exact prover disposes of those depths almost instantly by itself.
    // Preconditioning only the final depth measured 53/60 against 45/60 for
    // preconditioning every depth, and 49/60 for the depth-first route (23).
    //
    // Expressed as "the last iteration" rather than an absolute depth so it does
    // not depend on the problem depth: an absolute threshold tuned on mate-in-8
    // would silently disable preconditioning for shallower requests.
    bool dfpn_final_depth_only = true;
    // Absolute floor, retained for experiments. 1 means no floor.
    int dfpn_min_depth = 1;
    // Run the preconditioner when a VARIANT win rule is live? Default no.
    //
    // Proof numbers estimate how hard a node is to prove, and the estimate is
    // built entirely from the mate goal: it counts moves and replies and knows
    // nothing about a capture or check quota. Under a live quota that estimate
    // is not merely uninformative, it is actively misleading -- the search is
    // steered by a measure of the wrong game.
    //
    // Measured on the fourteen-position x-capture bench, with the verdict
    // identical in every case: the preconditioner costs between 9x and 30x the
    // wall clock and up to 10x the nodes. On plain directmates it PAYS (2x on
    // tests/mates.epd), so this stands the preconditioner down exactly where it
    // loses and nowhere else. See architecture 32.
    bool dfpn_under_variant = false;
    // Stream PROVEN intermediate bounds to stderr while the search runs.
    //
    // A long search is otherwise completely opaque: the depth-8 capture-quota
    // run produced its first and only output after twenty-nine minutes, and
    // progress had to be inferred from accumulated CPU time.
    //
    // Every line emitted is a theorem, not an estimate. That is the whole
    // difference between this and a playing engine's `info` output, which is a
    // running conjecture that may be withdrawn. Nothing here is ever withdrawn.
    bool progress = false;
    // Report E for both kings and stop. A diagnostic, so that the escape
    // count can be checked against hand-worked positions without a search.
    bool escape_count_only = false;
    // Also report WHICH root move is being searched, as it is picked up.
    //
    // Separate from `progress` because it is a different KIND of line. A bound
    // is a theorem; this is a status, superseded the moment the next move is
    // taken, and it asserts nothing whatever about the position. Keeping the
    // flags apart keeps a quiet stream of theorems available on its own.
    bool progress_moves = false;
    // May THIS search publish a bound? False for every restricted portfolio
    // lane, because a restricted lane that fails has proved nothing whatever --
    // it never looked at the moves the restriction removed. Carried as an
    // explicit flag rather than re-derived at the emit site so that it cannot
    // be got wrong by a future lane type that forgets to be checked.
    bool progress_authority = false;
    // Material floor for the preconditioner: it runs only when the position has
    // at least this many men. 0 disables the floor.
    //
    // Measured, not guessed. Over the 36 selfmate positions Chest solves and
    // this engine did not, running the preconditioner but IGNORING its hints
    // recovered nothing, while not running it at all recovered eight. The
    // guidance was never the problem -- the search could not afford to buy it.
    // Every one of the eight has eight men or fewer, and the preconditioner is
    // what wins at depth on ordinary material, so the fix is a floor rather than
    // a switch. See section 65.
    //
    // NOT the refuted experiment. That one chose a different ROUTE when material
    // was sparse and lost positions, because DFPN wins every piece-count bucket.
    // This changes whether the preconditioner RUNS, not which search follows it.
    // 7 is the only setting measured to gain without costing. The curve, over
    // the 36 selfmate positions Chest solves and this engine did not, against
    // mate-in-10 which is what the preconditioner exists for:
    //
    //     floor 0 (off)   0/36 selfmate   28/30 mate-in-10
    //     floor 7         3/36            28/30      <- free
    //     floor 9         7/36            26/30
    //     floor 11        8/36            25/30
    //     floor 13        8/36            23/30
    //
    // DEFAULT OFF, and the reason is a regression bar rather than a preference.
    //
    // The floor-7 row above was measured on a 30-position mate-in-10 SAMPLE and
    // read as free. On the full 200-position mate-in-8 corpus it is not: 177
    // against 178, so it LOSES a directmate position to gain three selfmates.
    // Net positive by two, and still the wrong trade -- buying sparse selfmates
    // with directmate is exactly what rejected the higher floors, and the agreed
    // regression bar is that a change may ADD verdicts and must not CHANGE them.
    //
    // The fifth time in this project that a sample has reversed on its corpus.
    // The flag stays so the curve is available to anyone who wants that end of
    // it; the default declines the trade.
    int dfpn_min_men = 0;
    // Weight applied to an AND node's proof estimate when the defender is not
    // in check, biasing DFPN toward forcing lines. 1 disables it. See
    // architecture 46 for why per-ply selectivity is the lever that compounds.
    int dfpn_check_bias = 1;
    int dfpn_epsilon_64 = 0;
    bool dfpn_sort = false;
    // Wall-clock budget in seconds. 0 means unlimited.
    // A deterministic alternative to --time-limit. Wall-clock budgets make every
    // comparison noisy: the same configuration measured 49, 51, 52 and 53 of 60
    // across this session purely on where positions fell relative to the clock,
    // which is the same size as the effects being measured. A node cap gives the
    // same answer on every run and every machine.
    //
    // Kept separate from node_budget, which the parallel cost gate sets and
    // clears for its own purposes and would otherwise wipe a user's limit.
    // How many positions from stdin to solve concurrently. 1 keeps answers
    // streaming as they are produced, which the service mode depends on.
    int parallel_positions = 1;
    std::uint64_t node_limit = 0;
    double time_limit = 0.0;
    // Begin iterative deepening at the requested depth instead of 1. Only valid
    // when the caller wants "a mate within N" rather than "the shortest mate".
    bool direct_depth = false;
    // WinChest ChecksOnly, a bitmask over the attacker's move choice:
    //    1  only own check-moves (serial-check mate)
    //    2  no opponent checks
    //    4  no opponent captures
    //    8  no own capture moves
    //   16  no own check-moves, except the mating move itself
    // Each selects a different problem, not a faster route to the same one.
    int checks_mask = 0;
    // WinChest KingSquares: allow only attacker moves after which the defender
    // king has at most this many squares available, counting the one it stands
    // on. 0 means off; 1 means the king cannot move at all.
    int king_squares = 0;
    // WinChest PieceLimit: at most this many defender pieces may have a legal
    // move after the attacker's move. 0 means off.
    int piece_limit = 0;
    // WinChest MaxMoves: the defender may have at most this many legal moves in
    // total after the attacker's move. 0 means off.
    int max_defender_moves = 0;
    // WinChest ThreatDepth. A move is examined only if, after it and a null
    // move by the defender, the attacker can mate within |threat_depth|.
    // Positive means the threat search uses check-moves only ("check threats");
    // negative allows any move ("quiet threats"). 0 is off.
    int threat_depth = 0;

    // Forcing restriction for a capture quota: 0 off, 1 captures only, 2
    // captures or checks. Attacker-side, so sound for proving and unable to
    // settle a disproof -- the same contract as every other lane restriction.
    int forcing_mode = 0;
    // Root mate depth, needed because WinChest caps ThreatDepth at depth - 2.
    int root_depth = 0;
    // Try a sequence of restricted searches within the time budget, falling
    // back to the unrestricted one. Sound because a restriction only removes
    // attacker options: a mate found under one is a real forced mate.
    bool portfolio = true;
    // Run the portfolio entries concurrently instead of in sequence, so each
    // gets the whole budget rather than a slice. Sound for the same reason the
    // sequential portfolio is, and it uses cores that root splitting cannot:
    // the entries solve different restricted problems, so unlike root-split
    // workers they do not duplicate each other's search.
    bool portfolio_parallel = true;
    // Cap on concurrent portfolio lanes. 0 means "every lane". Lanes contend
    // for cores and memory bandwidth, so more lanes is not monotonically
    // better: a lane that solves a position in 7.4 s alone can take 27 s
    // against eight others. See section 57.
    int portfolio_lanes = 0;
    // Threads given to each route-diversity lane. 0 takes the built-in default.
    // The split scales that lane but steals cores from the lanes that resolve
    // most positions quickly, so this is a bounded share. See section 57.
    int route_lane_threads = 0;
    // Enumerate EVERY root move that solves, not just the first.
    //
    // A second solution at the root is a dual, and a directmate with one is
    // cooked. This is the composition question rather than the prover's, and it
    // defeats the first-proof short-circuit at the root deliberately. See
    // section 59.
    bool all_solutions = false;
    // Analyse each successor position instead of this one (Chest's -x): "black
    // moves, but loses in N".
    bool successors = false;
    // Check the position for legality and report, without searching (-c).
    bool legality_only = false;
    // Print the solution tree rather than a single line (-L), and in short
    // algebraic rather than coordinates (-S).
    bool print_tree = false;
    bool short_notation = false;
    // Extend the printed tree by this many full moves beyond the proof (-l).
    // The proof stops at mate; an analyst sometimes wants the line continued.
    int tree_extra_moves = 0;
    // Dual suppression level, counted in full moves from the leaves (-u), and
    // complete suppression except at the top level (-U). These control how much
    // of a deep tree is printed, not what is searched: top-level duals are the
    // soundness verdict and are never suppressed.
    int dual_suppression = 0;
    bool suppress_all_duals = false;
    // Depth used when the input line carries none (-Z), as distinct from -z
    // which OVERRIDES whatever the line says.
    int default_depth = 0;
    // Store the principal variation and the certificate in table entries.
    //
    // The search never reads them: probes consume the depth bounds and the
    // refuted flag, and the line is wanted once, at the end. Off makes entries
    // smaller, so more verdicts fit in the same budget -- see architecture 114
    // for whether that is worth the truncated line it costs.
    // Share PROOFS, and only proofs, between the portfolio's lanes.
    //
    // A restricted lane removes ATTACKER options. So its proof is valid
    // unrestricted -- the same strategy still works when more moves are
    // available -- while its DISPROOF is valid for nothing but its own
    // restricted problem, because it never looked at the moves the restriction
    // removed. That asymmetry is the entire design: proofs flow into a table
    // every lane can read, disproofs never leave the lane that made them.
    //
    // DEFAULT OFF until the gate has run on a corpus. The hazard is not
    // validity, it is MINIMALITY: probe_exact_proof_table's stated precondition
    // is that a proof depth is minimal, and a restricted lane can prove a
    // LONGER mate than exists because its restriction forbids the short one. A
    // non-minimal bound still answers "is there a mate within d" correctly, so
    // no verdict can be wrong; what could move is the reported DEPTH, and a
    // wrong `dm N` is a wrong answer even when the mate is real. See
    // architecture 116 and test_cross_lane_proofs_do_not_move_the_depth.
    //
    // ON by default, on the gate rather than on the argument. Over 72 positions
    // -- the shipped corpus, 30 mate-in-8s and 30 selfmates -- no verdict and no
    // depth moved, and one mate-in-8 went from timing out to solved. That is the
    // regression bar exactly: a change may ADD verdicts and must not CHANGE
    // them.
    bool cross_lane_proofs = true;
    // The same table, carried between JOBS as well as between lanes.
    //
    // Sound for exactly the reason cross-lane sharing is: the key is complete
    // -- board, side to move, attacker, node kind, castling, en passant, goal,
    // and the check and capture quotas -- so an entry is a fact about a
    // POSITION and not about the root it was reached from. tt_key's own comment
    // notes that the capture quotas are keyed although they are derivable,
    // precisely because the derivation "holds only while a table never spans
    // two roots, which is true today and enforced nowhere". This is the change
    // that makes it no longer true, and the bits were already paid for.
    //
    // ONE PRECONDITION, and it is not in the key. The escape limit is
    // deliberately unkeyed on the argument that it is "a CONSTANT of the
    // search, identical at every node a table ever holds". Across jobs that
    // argument needs the limit to be constant across the BATCH too, which it is
    // when it comes from the command line and is not when a per-line Forsyth
    // field overrides it. So the carry-over stands down whenever an escape rule
    // is live, rather than relying on nobody mixing limits in one file.
    bool cross_job_proofs = true;
    // A table carried in from an enclosing job, replacing the one a portfolio
    // would otherwise build for itself. Null for an ordinary run.
    SharedProofTable* job_table = nullptr;
    // A worker waiting for its own split's helpers goes and helps others rather
    // than idling, instead of being a lost thread. See claim_any_child for the
    // termination argument -- the reason this is safe is the valuable part.
    //
    // DEFAULT OFF, because it was measured to have nothing to do. It was named
    // the highest-ceiling parallel item on the reasoning that every extra split
    // point converts a worker into a waiter; 113's demand gating had already
    // removed that, by never creating split points nobody is waiting for. At the
    // shipped floor the mechanism fires 98 times in a whole depth-7 search
    // (3,102 helped claims against 3,004 without it) and the wall clock does not
    // move: 5.37/5.30 s against 5.29/5.27 s.
    //
    // Kept because it costs nothing when off, because the proof is written down,
    // and because any future attempt at a finer decomposition needs it. See
    // architecture 114.
    bool owner_helps = false;
    bool tt_lines = true;
    // Choose eviction victims by the depth bound they carry rather than by
    // whatever the map iterator reaches first.
    //
    // DEFAULT OFF: measured slightly WORSE, consistently. Depth is a good proxy
    // for what an entry cost to compute and a bad one for whether it will be
    // wanted again, and the second is what a cache is for. Shallow entries are
    // enormously more numerous and are re-probed constantly; deep ones are
    // expensive and often probed once. The generation-aged policy already keeps
    // what is being USED, which is the better signal. See architecture 114.
    bool tt_depth_evict = false;
    bool shared_tt = true;
    std::size_t shared_tt_shards = 256;
    std::uint64_t parallel_min_nodes = 500;
    // -M megabytes. Converted to an entry ceiling via EST_BYTES_PER_ENTRY.
    // 64 MB sat well below the knee: at mate-in-8 it evicts hard enough to cost
    // throughput, and the budget's fixed overhead is proportionally worst there
    // (a 64 MB request peaks near 91 MB RSS). See architecture 8l.
    //
    // When the user passes -M, this is the budget for every table alive at
    // once, divided across the portfolio's lanes and the batch's position
    // workers (architecture 42). Left alone, it is the budget for each table
    // and the total scales with the number of tables -- so the default is
    // exactly the tuning above at any lane and worker count, and cannot be
    // eroded by raising --parallel-positions. Splitting a fixed total would
    // have handed each table 32 MB at eight workers, half of the 64 MB the
    // comment above records as already well below the knee.
    //
    // Same shape as `--threads auto`: the default adapts, an explicit value is
    // obeyed literally.
    // UCI `stop`, and anything else that must abandon a whole run from outside.
    //
    // On the CONFIG rather than on Search, which is what makes it one edit
    // instead of a plumbing exercise: every worker and every portfolio lane
    // copies the config wholesale, so a flag here is already visible to every
    // Search in the program without a single pointer being threaded anywhere.
    //
    // Raising it is an ABORT, never a verdict. search_cancelled sets
    // `timed_out` alongside `aborted`, so a stopped search reports "gave up"
    // and can never be read as a disproof -- which is the invariant that makes
    // it safe to stop a prover at all.
    const std::atomic<bool>* stop_flag = nullptr;
    // Fraction of the table shed per eviction, as 1/N. See BoundedTable: this
    // is the cost of the SCAN, not a memory setting, and it was worth 1.95x on
    // a deep search. Architecture 121.
    // Direct-mapped flat proof table instead of a hash map. Off until measured;
    // see architecture 122.
    bool flat_tt = true;
    std::size_t tt_shed_divisor = 2;
    std::size_t memory_mb = 256;
    bool memory_is_total = false;
};

struct PnDnFwd {
    std::uint32_t pn = 1;
    std::uint32_t dn = 1;
};

// Per-search mutable state. Inheriting the config keeps every existing
// `s.<option>` access valid while making the option set independently
// copyable.
struct Search : SearchConfig {
    // The depth of the iterative-deepening pass now running. The ROOT attacker
    // node is the only one whose remaining depth equals it, which is how the
    // root-move stream recognises the root without threading a flag down the
    // recursion.
    int iteration_depth = 0;
    // When this search began. Only the progress stream reads it, and only to
    // put a wall-clock figure beside each proven bound so the growth per depth
    // is visible as it happens. Nothing in the search depends on it.
    std::chrono::steady_clock::time_point search_start{std::chrono::steady_clock::now()};
    Stats stats;
    BoundedTable tt;
    std::unordered_map<TTKey, Move, TTKeyHash> defender_refutations;
    std::unordered_map<TTKey, Move, TTKeyHash> attacker_proofs;

    // Cooperative cancellation. `cancel` is null for an ordinary
    // single-threaded search, so the per-node check costs one null test and
    // the abort path is unreachable. When a search is abandoned, `aborted`
    // records that its result means "gave up", not "disproved" -- an aborted
    // result must never be stored in a proof table or read as a refutation.
    const std::atomic<bool>* cancel = nullptr;
    // A second, outer cancellation flag.
    //
    // A root-split worker's own `cancel` is its slot's flag, set by a sibling
    // that proved a better root move. That says nothing about whether the
    // SEARCH THIS SPLIT BELONGS TO has been abandoned -- a portfolio lane that
    // lost the race, say. Without this the workers ran on to their deadline,
    // and since their deadline is the run's time limit, a position whose answer
    // was already in hand still took the full limit to report. See section 57.
    const std::atomic<bool>* external_cancel = nullptr;
    bool aborted = false;

    // Absolute node ceiling for this search. Zero means unbounded. Used by the
    // sequential prelude of the parallel cost gate: exceeding the ceiling is an
    // abort, not a verdict, so nothing false is recorded.
    std::uint64_t node_budget = 0;

    // Wall-clock deadline. Expiry is an abort, which by the abort invariant
    // records no verdict, so a timed-out search reports "not proved" rather
    // than anything false. Shared by value with every parallel worker, so all
    // of them stop at the same instant.
    bool has_deadline = false;
    std::chrono::steady_clock::time_point deadline{};
    bool timed_out = false;
    // GAP-1's verdict, carried out to the result line. Set ONLY from a lane that
    // searched unrestricted: a restricted lane's failure is silent about the
    // moves it was not allowed, so it can no more assert "no solution at any
    // depth" than it can assert "no solution".
    bool refuted_any_depth = false;
    std::uint32_t deadline_countdown = 0;

    // When non-null, exact proof entries live in a table shared with the other
    // workers of this search instead of in the private `tt` map above.
    SharedProofTable* shared_table = nullptr;

    // Where this search publishes its split points, and the slot it owns there.
    // Null for an ordinary sequential search, which is what makes the split
    // path in prove_attacker cost one null test when nobody is parallelising.
    // One slot per worker, so a worker can have at most one split open and can
    // never be waiting on a node it is itself the owner of.
    SplitRegistry* split_registry = nullptr;
    int split_slot = -1;
    // Proofs shared with the other portfolio lanes. PROOFS ONLY: nothing on the
    // disproof side of the lattice is ever written here or read from here, which
    // is what makes it sound for a restricted lane to contribute.
    SharedProofTable* cross_proofs = nullptr;
    // May THIS search contribute DISPROOFS to the cross table?
    //
    // True only for an unrestricted lane, and the asymmetry runs the opposite
    // way to the proofs'. A restriction removes attacker options, so:
    //
    //   restricted PROOF    -> valid unrestricted. A mate forced with fewer
    //                          options is a mate forced with more.
    //   restricted DISPROOF -> valid nowhere else. It never looked at the moves
    //                          the restriction removed.
    //   unrestricted PROOF  -> valid everywhere, trivially.
    //   unrestricted DISPROOF -> valid for RESTRICTED lanes too. "No mate
    //                          within d with every move available" implies "no
    //                          mate within d with fewer".
    //
    // So three of the four directions are sound and one is not, and this flag is
    // the one that is not. Disproofs are 93% of what the table holds, so this is
    // where the sharing is worth anything at all.
    bool cross_authoritative = false;
    // Which root move this worker is serving. Helpers prefer the lowest, which
    // is the order the sequential search would reach these subtrees in, and the
    // root split's existing cancellation reads it to stop everyone working on a
    // root move a lower index has already beaten.
    int split_priority = 0;
    // Sequence number of the newest split this search owns, 0 if none. A worker
    // may only take children from splits published after this, which is what
    // lets a blocked owner help without the wait relation ever forming a ring.
    std::uint64_t max_owned_seq = 0;

    // Proof/disproof numbers for the DFPN route. Purely a heuristic cache:
    // discarding it costs search effort and cannot change a verdict.
    std::unordered_map<TTKey, PnDnFwd, TTKeyHash> dfpn_tt;
    std::size_t dfpn_capacity = 1u << 21;

    // Context for ThreatDepth probes. The threat search asks "can the attacker
    // mate within R from this position", optionally restricted to checks, which
    // is a DIFFERENT predicate from the enclosing search. Sharing a table
    // between them would let a restricted disproof answer an unrestricted
    // question, so it gets its own Search and its own table.
    std::unique_ptr<Search> threat_ctx;

    void reset_for_new_search() {
        aborted = false;
    }
};

// Returns true if this search must unwind. Sets `aborted` so that every
// enclosing frame skips its table stores and refuses to interpret the empty
// result as a disproof.
inline bool search_cancelled(Search& s) {
    if (s.aborted) {
        return true;
    }
    if (s.node_budget != 0 && s.stats.nodes >= s.node_budget) {
        s.aborted = true;
        return true;
    }
    if (s.node_limit != 0 && s.stats.nodes >= s.node_limit) {
        // timed_out, not merely aborted. Section 8r: a line with no marker means
        // "searched exhaustively, no mate exists". An exhausted node budget has
        // settled nothing, so it must report the same "gave up" outcome a
        // wall-clock expiry does, or it would be read as a disproof.
        s.timed_out = true;
        s.aborted = true;
        return true;
    }
    // Reading the clock on every node would cost more than the search it
    // guards, so the deadline is polled on a countdown. The granularity only
    // affects how promptly the limit is honoured, never correctness.
    if (s.has_deadline) {
        if (s.deadline_countdown == 0) {
            s.deadline_countdown = 2048;
            ++s.stats.deadline_checks;
            if (std::chrono::steady_clock::now() >= s.deadline) {
                s.timed_out = true;
                s.aborted = true;
                return true;
            }
        } else {
            --s.deadline_countdown;
        }
    }
    if (s.stop_flag != nullptr && s.stop_flag->load(std::memory_order_relaxed)) {
        // timed_out as well as aborted: section 8r's rule is that a line with no
        // marker means "searched exhaustively, no mate exists", and a stopped
        // search has settled nothing.
        s.timed_out = true;
        s.aborted = true;
        return true;
    }
    if (s.cancel != nullptr && s.cancel->load(std::memory_order_relaxed)) {
        s.aborted = true;
        return true;
    }
    if (s.external_cancel != nullptr && s.external_cancel->load(std::memory_order_relaxed)) {
        s.aborted = true;
        return true;
    }
    return false;
}

bool is_white_piece(char p) {
    return p >= 'A' && p <= 'Z';
}

bool is_black_piece(char p) {
    return p >= 'a' && p <= 'z';
}

bool is_piece_color(char p, Color c) {
    return c == WHITE ? is_white_piece(p) : is_black_piece(p);
}

bool is_enemy_piece(char p, Color c) {
    return p != '.' && !is_piece_color(p, c);
}

bool is_king_piece(char p) {
    return p == 'K' || p == 'k';
}

std::uint8_t piece_code(char p) {
    switch (p) {
        case '.': return 0;
        case 'P': return 1;
        case 'N': return 2;
        case 'B': return 3;
        case 'R': return 4;
        case 'Q': return 5;
        case 'K': return 6;
        case 'p': return 7;
        case 'n': return 8;
        case 'b': return 9;
        case 'r': return 10;
        case 'q': return 11;
        case 'k': return 12;
        default: return 0;
    }
}

void set_square(Board& b, int sq, char p) {
    const std::uint64_t bit = 1ull << sq;
    const char old = b.sq[sq];
    if (type_of(old) != PT_NONE) {
        b.occ &= ~bit;
        b.by_color[is_white_piece(old) ? WHITE : BLACK] &= ~bit;
        b.by_type[type_of(old)] &= ~bit;
    }
    if (type_of(p) != PT_NONE) {
        b.occ |= bit;
        b.by_color[is_white_piece(p) ? WHITE : BLACK] |= bit;
        b.by_type[type_of(p)] |= bit;
    }
    b.sq[sq] = p;
    int word = sq / 16;
    int shift = (sq % 16) * 4;
    std::uint64_t mask = 0xfull << shift;
    b.packed[word] = (b.packed[word] & ~mask) | (static_cast<std::uint64_t>(piece_code(p)) << shift);
}

Color other(Color c) {
    return c == WHITE ? BLACK : WHITE;
}

int file_of(int sq) {
    return sq & 7;
}

int rank_of(int sq) {
    return sq >> 3;
}

bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

int square_of(int file, int rank) {
    return rank * 8 + file;
}

} // namespace mateprover

#endif // MATEPROVER_SEARCH_STATE_H_INCLUDED
