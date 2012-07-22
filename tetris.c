#include "core.c"
#include "states.c"

/* Who wins? */
#define AI 0
#define PLAYER 1

/* set to 1 to print stuff out */
#define SHOW 0

/* set to 1 to print timings out every INTERVAL seconds */
#define TIME 1
#define INTERVAL 1

/******************************************************************************/
/* Printing stuff out. We have to track our progress manually */

time_t start;   /* time at which execution started */
time_t last;    /* time at which last timestamp was printed */
time_t current; /* current time */
time_t end;     /* time at which execution ended */

short well[FULLHEIGHT];
short depth = 0;

#define getNumStates()                              numStates[getPieceId()]
#define getState(stateId)                           states[getPieceId()][stateId]
#define setStateReachability(stateId, reachability) states[getPieceId()][stateId].reachable = reachability

/* this is where we maintain our stack of current piece IDs */
short pieceIds[MAXDEPTH];
#define getPieceId()        pieceIds[depth]
#define incrementPieceId()  pieceIds[depth]++
#define setPieceId(pieceId) pieceIds[depth] = pieceId

/* The next three variables are where we maintain our stack of current landing IDs */
int numLandings[MAXDEPTH];
#define getNumLandings()       numLandings[depth]
#define incrementNumLandings() numLandings[depth]++
#define setNumLandings(num)    numLandings[depth] = num

int landingIds[MAXDEPTH];
#define getLandingId()          landingIds[depth]
#define incrementLandingId()    landingIds[depth]++
#define setLandingId(landingId) landingIds[depth] = landingId

int landings[MAXDEPTH][MAXSTATES];
#define getLanding(landingId)           landings[depth][landingId]
#define saveLanding(landingId, stateId) landings[depth][landingId] = stateId

/******************************************************************************/
/* printables */

/* print out the percentage completion and estimated time to full exhaustion of
all possibilities. This can be derived by examining the
percentage progress through each stack at each level of depth. There are two
interleaved stacks: one of landing states/wells, and one of pieces to be inserted
into each well. */
void dumpProgress(void) {
	int d = 0;
	double completion = 0;
	double scale = 1.0;
	for(d = 0; d < depth; d++) {
		completion += scale * (double) pieceIds[d] / (double) NUMPIECES;
		scale /= (double) NUMPIECES;
		// printf("%d/%d = %.10f (%.10f)\n", pieceIds[d], NUMPIECES, (double) pieceIds[d] / (double) NUMPIECES, completion);

		completion += scale * (double) landingIds[d] / (double) numLandings[d];
		scale /= (double) numLandings[d];
		// printf("%d/%d = %.10f (%.10f)\n", landingIds[d], numLandings[d], (double) landingIds[d] / (double) numLandings[d], completion);
	}
	int elapsed = current - start;
	printf("%.10f%% complete in %d seconds, %d seconds total\n", completion * 100, elapsed, (int) ((double) elapsed / completion));
	return;
}

/* show details about this state */
void printState(int stateId) {
	struct State state = getState(stateId);
	printf("pieceId = %d, stateId = %d\n", getPieceId(), stateId);

	short y = 0;
	for(y = state.yTop; y < state.yBottom; y++) {
		printf("grid[%d] is %d\n", y, state.grid[y]);
	}

	short keyId = 0;
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		printf("next[%d] is %d\n", keyId, state.next[keyId]);
	}

	printf("outOfBounds: %s\n",   state.outOfBounds   ? "TRUE" : "FALSE");
	printf("overlapsSpawn: %s\n", state.overlapsSpawn ? "TRUE" : "FALSE");
	return;
}

/* show the state as it appears in the well */
void show(int stateId) {
	short x = 0;
	short y = 0;

	system("cls");
	printf("+");
	for(x = 0; x < WIDTH; x++) {
		printf("-");
	}
	printf("+\n");
	for(y = 0; y < FULLHEIGHT; y++) {

		/* horizontal bar indicates "invisible" rows above main playing field */
		if(y == SPARELINES) {
			printf("+");
			for(x = 0; x < WIDTH; x++) {
				printf("-");
			}
			printf("+\n");
		}

		printf("|");
		/* have to iterate backwards, from x = 9 to x = 0 */
		for(x = WIDTH - 1; x >= 0; x--) {
			if(
				   0 <= stateId
				&& stateId < getNumStates()
				&& getState(stateId).grid[y] >> x & 1
			) {
				printf("@");
			} else if(well[y] >> x & 1) {
				printf("#");
			} else {
				printf(".");
			}
		}
		printf("|\n");
	}

	printf("+");
	for(x = 0; x < WIDTH; x++) {
		printf("-");
	}
	printf("+\n");

	if(
			 0 <= stateId
		&& stateId < getNumStates()
	) {
		printState(stateId);
	}
	
	return;
}

/******************************************************************************/
/* Actually computing all the reachable wells and Solutions for each */

/* is there room in the current well for a piece in this state (FALSE) or is there an
obstruction (TRUE) ? */
char collision(int stateId) {
	short y = 0;
	for(y = getState(stateId).yTop; y < getState(stateId).yBottom; y++) {
		if(well[y] & getState(stateId).grid[y]) {
			return TRUE;
		}
	}
	return FALSE;
}

/* In the current well, using the current piece, starting from the supplied State,
find all reachable legitimate (i.e. non-deadly)
landing sites, recursively. If a line can be made from here, exit early and return TRUE.
If not, populate the full list and return FALSE. */
char populateLandings(int stateId) {
	short y = 0;
	int nextStateId = 0;

	/* This state is reachable... */
	setStateReachability(stateId, 1);

	/* and investigate the rest (i.e. Perform The Calculations) */
	short keyId = 0;
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		nextStateId = getState(stateId).next[keyId];

		if(
			/* move down: have to consider possibility that this could be a landing
			state */
			keyId == downKeyId

			/* If there's no way to move down from this point, we must look at
			the current State as a landing point. */
			&& (
					 nextStateId == stateId
				|| collision(nextStateId) == TRUE
			)
		) {

			/* If the State is completely outside of the
			visible playing field, then landing it at this
			location yields an instant death and player loss.
			*/
			if(getState(stateId).outOfBounds == TRUE) {
				continue;
			}

			/* Otherwise, this is a State where the player can
			legally land the current piece. */
			#if SHOW
				show(stateId);
				printf("Here's a landing\n");
				getchar();
			#endif

			/* Has a line been made? */
			for(y = getState(stateId).yTop; y < getState(stateId).yBottom; y++) {
				/* a line has been made! */
				if((well[y] | getState(stateId).grid[y]) == LINETRIGGER) {
					return TRUE;
				}
			}

			/* No line has been made. */

			/* If this State overlaps the spawning box then the AI can simply
			spawn a new piece to overlap IT, resulting in an immediate AI win. */
			if(getState(stateId).overlapsSpawn == TRUE) {
				continue;
			}

			/* Then this landing State merely yields another boring well. Save to memory. */
			saveLanding(getNumLandings(), stateId);

			/* Shift up */
			incrementNumLandings();
		}

		/* otherwise, we have either (1) moved down successfully or (2) moved in some other
		direction, so examine the possibilities */
		else {

			/* next State is already marked "reachable"? Then all 
			calculations for it have already been done. NEVER MIND */
			if(getState(nextStateId).reachable == TRUE) {
				continue;
			}
			
			/* Collision: ignore it */
			if(collision(nextStateId) == TRUE) {
				continue;
			}

			/* Legit reachable state. Do calculations for it! */
			if(populateLandings(nextStateId)) {
				return TRUE;
			}
		}
	}
	
	/* If we get all the way here then the list is populated and no line can be made
	using this piece */
	return FALSE;
}

char wellWinner(void);
char pieceWinner(void);

/* Find a landing state where landing the current piece would result in an
immediate line (or, if the player plays rationally afterwards, an eventual
guaranteed line) and return PLAYER if found. If no such state can be
found, return AI. */
char pieceWinner(void) {

	/* assume every State is unreachable to begin with */
	int stateId = 0;
	for(stateId = 0; stateId < getNumStates(); stateId++) {
		setStateReachability(stateId, 0);
	}

	setNumLandings(0);

	/* Is it possible to get an immediate line? If so, return that landing
	state */
	if(populateLandings(0)) {
		#if SHOW
			show(0);
			printf("PLAYER wins this well with this piece\n");
			getchar();
		#endif
		return PLAYER;
	}
	
	/* Otherwise, iterate over all landing sites looking for further strategy */
	short y = 0;
	struct State state;
	char whoWins;
	for(setLandingId(0); getLandingId() < getNumLandings(); incrementLandingId()) {
		state = getState(getLanding(getLandingId()));

		/* add the piece to the well */
		/* check out the high optimisation here */
		for(y = state.yTop; y < state.yBottom; y++) {
			well[y] |= state.grid[y];
		}
		
		depth++;
		whoWins = wellWinner();
		depth--;

		/* undo that well modification */
		for(y = state.yTop; y < state.yBottom; y++) {
			well[y] &= ~state.grid[y];
		}

		/* PLAYER wins */
		if(whoWins == PLAYER) {
			#if SHOW
				show(0);
				printf("PLAYER wins this well with this piece\n");
				getchar();
			#endif

			return PLAYER;
		}
		
		/* AI wins: continue */
	}

	/* if we reach this stage then there is no way that a player can force a
	line after this point - the player dies with no lines. */
	#if SHOW
		show(0);
		printf("AI wins this well with this piece\n");
		getchar();
	#endif
	return AI;
}

/* Supplied with a well, compute the winner. */
char wellWinner(void) {

	#if TIME
		time(&current);
		if(current - last >= INTERVAL) {
			last = current;

			/* time-based readouts go here */
			dumpProgress();
		}
	#endif

	/* depth is populated already */
	for(setPieceId(0); getPieceId() < NUMPIECES; incrementPieceId()) {
	
		/* AI wins by supplying this piece? */
		if(pieceWinner() == AI) {
			#if SHOW
				show(-1);
				printf("AI wins this well\n");
				getchar();
			#endif
			return AI;
		}
	}

	#if SHOW
		show(-1);
		printf("PLAYER wins this well\n");
		getchar();
	#endif
	return PLAYER;
}

/******************************************************************************/

int main(int argc, char ** argv) {

	/* Initialise well to emptiness state */
	short y = 0;
	for(y = 0; y < FULLHEIGHT; y++) {
		well[y] = 0;
	}
	
	/* solve the empty well */
	time(&start);
	time(&last);
	printf("Time is %s", asctime(localtime(&start)));
	char whoWins = wellWinner();
	time(&end);

	/* Output */
	printf("WIDTH = %d, HEIGHT = %d\n", WIDTH, HEIGHT);
	if(whoWins == AI    ) { printf("AI wins\n"    ); }
	if(whoWins == PLAYER) { printf("PLAYER wins\n"); }
	printf("Processing took %d second(s)\n", (int) (end - start));

	return 0;
}