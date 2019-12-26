#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define PIECESIZE 4
#define NUMPIECES 7
#define NUMORIENTATIONS 19
#define NUMCONTROLS 4
#define BOXSIZE 4

#define WIDTH 12
#define HEIGHT 4

#define LINETRIGGER ((1 << WIDTH) - 1)
#define FULLHEIGHT (BOXSIZE + HEIGHT)
#define MAXSTATES (NUMPIECES * (WIDTH - 1) * (FULLHEIGHT - 1) * 4)
#define MAXDEPTH (HEIGHT * (WIDTH - 1) / PIECESIZE)
#define MAXOVERLAPS ((BOXSIZE + BOXSIZE - 1) * (BOXSIZE + BOXSIZE - 1) * NUMORIENTATIONS)

char pieceNames[NUMPIECES] = {'O', 'I', 'J', 'L', 'S', 'T', 'Z'};

/* Define our piece orientations. This is the right-handed Nintendo Rotation
System with a raised horizontal I piece, also known as the Original Rotation
System. There are no wall or floor kicks, for optimisation reasons.
http://tetris.wikia.com/wiki/ORS */
short firstOrientationId[NUMPIECES];
short numOrientations[NUMPIECES] = {1, 2, 4, 4, 2, 4, 2};
short allMasks[NUMORIENTATIONS][BOXSIZE] = {
	{0,6,6,0},  /* O */
	{0,15,0,0}, /* I */
	{2,2,2,2},  /* I */
	{0,14,2,0}, /* J */
	{4,4,12,0}, /* J */
	{8,14,0,0}, /* J */
	{6,4,4,0},  /* J */
	{0,7,4,0},  /* L */
	{6,2,2,0},  /* L */
	{1,7,0,0},  /* L */
	{2,2,3,0},  /* L */
	{0,3,6,0},  /* S */
	{2,3,1,0},  /* S */
	{0,7,2,0},  /* T */
	{2,6,2,0},  /* T */
	{2,7,0,0},  /* T */
	{2,3,2,0},  /* T */
	{0,6,3,0},  /* Z */
	{1,3,2,0}   /* Z */
};

/* store piece configurations */
short well[FULLHEIGHT];

/* There is a static collection of only about 4000 possible positions and
orientations that a piece can hold in the well. We generate a complete list
of these. Each one has pointers to all the "next" states obtained by pressing
a key at this point, as well as as much pre-calculated data as is practical. */
struct State {
	/* irritatingly, every state must know what piece it is, and its
	orientation */
	short pieceId, orientationId;

	/* number of grid squares of gap between the top right corner of the well
	and the top right corner of the 4x4 bounding box of every state */
	short xPos, yPos;

	/* number of grid squares of gap between the top right of the 4x4 bounding
	box and where the piece itself begins */
	short xOffset, yOffset;

	/* size of the piece in grid squares */
	short xDim, yDim;

	/* this stuff is all pre-calculated to make landing calculations fast fast
	FAST. */
	short yTop, yBottom;
	short grid[FULLHEIGHT];

	/* pointers to other states in memory which would be obtained by taking
	the named actions. Possibly a pointer to self if this action is not
	permitted (e.g. attempt to move off side of screen) */
	struct State * nextPtrs[NUMCONTROLS];

	/* pointers to other states in memory which overlap this one. */
	// struct State * overlapPtrs[MAXOVERLAPS];
	// short numOverlaps;

	/* is this state currently reachable (1) or not (0)? */
	short reachable;
};

/* Constructor requires only a piece ID, orientation ID, xPos and yPos.
Everything else is generated from these. */
struct State constructState(short pieceId, short orientationId, short xPos, short yPos) {
	short maskId = 0;
	short allMask = 0;
	short keyId = 0;
	short x, y;
	short * masks = allMasks[orientationId];

	struct State state;

	/* import knowns */
	state.pieceId = pieceId;
	state.orientationId = orientationId;
	state.xPos = xPos;
	state.yPos = yPos;

	/* find first non-empty mask */
	maskId = 0;
	while(masks[maskId] == 0 && maskId < BOXSIZE) {
		maskId++;
	}
	state.yOffset = maskId;

	/* find first empty mask */
	while(masks[maskId] > 0 && maskId < BOXSIZE) {
		allMask |= masks[maskId];
		maskId++;
	}
	state.yDim = maskId - state.yOffset;

	/* find right side of masks */
	state.xOffset = 0;
	while((allMask & 1) == 0) {
		allMask >>= 1;
		state.xOffset++;
	}

	/* find left side of masks */
	state.xDim = 0;
	while((allMask & 1) == 1) {
		allMask >>= 1;
		state.xDim++;
	}

	/* pointers are populated later */
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		state.nextPtrs[keyId] = NULL;
	}

	/* pre-calc stuff */
	state.yTop = state.yPos + state.yOffset;
	state.yBottom = state.yPos + state.yOffset + state.yDim;

	/* this loop is more time-consuming than it needs to be but feh, it is
	done once ever */
	for(y = 0; y < FULLHEIGHT; y++) {
		state.grid[y] = 0;
		for(x = 0; x < WIDTH; x++) {
			if(
				   state.yPos <= y
				&& y < state.yPos + BOXSIZE
				&& state.xPos <= x
				&& x < state.xPos + BOXSIZE
				&& masks[y - state.yPos] >> (x - state.xPos) & 1
			) {
				state.grid[y] |= 1 << x;
			}
		}
	}

	return state;
}

/* show details about this state */
void print(struct State * statePtr) {
	short y, keyId;
	printf("I am piece #%d, '%c', in orientation %d @ %d\n", statePtr->pieceId, pieceNames[statePtr->pieceId], statePtr->orientationId, statePtr);
	printf("(xPos, yPos) is (%d, %d)\n", statePtr->xPos, statePtr->yPos);
	printf("(xOffset, yOffset) is (%d, %d)\n", statePtr->xOffset, statePtr->yOffset);
	printf("(xDim, yDim) is (%d, %d)\n", statePtr->xDim, statePtr->yDim);
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		printf("nextPtrs[%d] is %d\n", keyId, statePtr->nextPtrs[keyId]);
	}
	for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
		printf("grid[%d] is %d\n", y, statePtr->grid[y]);
	}
	// printf("I overlap with %d states including myself\n", statePtr->numOverlaps);
	return;
}

/* show the state as it appears in the well */
void show(struct State * statePtr) {
	short x, y;

	// system("cls");
	printf("+");
	for(x = 0; x < WIDTH; x++) {
		printf("-");
	}
	printf("+\n");
	for(y = 0; y < FULLHEIGHT; y++) {

		/* horizontal bar leaves room for full rotations beforehand */
		if(y == BOXSIZE) {
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
				   statePtr != NULL
				&& statePtr->grid[y] >> x & 1
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

	if(statePtr != NULL) {
		print(statePtr);
	}
	return;
}

/* return the state after R key */
struct State right(struct State state) {
	/* can't shift off the right side of the screen */
	if(state.xPos + state.xOffset <= 0) {
		return state;
	}

	/* success */
	return constructState(state.pieceId, state.orientationId, state.xPos - 1, state.yPos);
}

/* return the state after L key */
struct State left(struct State state) {
	/* can't shift off the left side of the screen */
	if(state.xPos + state.xOffset + state.xDim >= WIDTH) {
		return state;
	}

	/* success */
	return constructState(state.pieceId, state.orientationId, state.xPos + 1, state.yPos);
}

/* try to move piece down */
struct State down(struct State state) {
	/* can't shift below the bottom of the well */
	if(state.yPos + state.yOffset + state.yDim >= FULLHEIGHT) {
		return state;
	}

	/* success */
	return constructState(state.pieceId, state.orientationId, state.xPos, state.yPos + 1);
}

/* try to rotate the piece */
struct State rotate(struct State state) {
	short x, y;
	struct State newState;

	short newOrientationId = state.orientationId + 1;
	if(newOrientationId >= firstOrientationId[state.pieceId] + numOrientations[state.pieceId]) {
		newOrientationId = firstOrientationId[state.pieceId];
	}

	newState = constructState(state.pieceId, newOrientationId, state.xPos, state.yPos);

	/* check boundaries */
	if(
		   newState.xPos + newState.xOffset < 0
		|| newState.xPos + newState.xOffset + newState.xDim > WIDTH
		|| newState.yPos + newState.yOffset < 0
		|| newState.yPos + newState.yOffset + newState.yDim > FULLHEIGHT
	) {
		return state;
	}

	return newState;
}

/* "controls" is the name of an array. The number of elements in the array is
NUMCONTROLS. Each element is a pointer. Each pointer points to a function. Each
function takes a struct State as its argument and returns a struct State. */
struct State (* controls[NUMCONTROLS])(struct State) = {
	rotate,
	left,
	right,
	down
};

/* complete list of all possible states */
struct State states[MAXSTATES];
short numStates = 0;

/* list of states where new pieces are instantiated. */
struct State * startPtrs[NUMPIECES];

/* compare two States to avoid duplicates in the listing. The only
distinguishing features are piece ID, orientation, xPos and yPos.
Everything else is derived. */
short equal(struct State p1, struct State p2) {
	short maskId;
	if(p1.pieceId != p2.pieceId) {
		return 0;
	}
	if(p1.orientationId != p2.orientationId) {
		return 0;
	}
	if(p1.xPos != p2.xPos) {
		return 0;
	}
	if(p1.yPos != p2.yPos) {
		return 0;
	}
	return 1;
}

/* See if a State already exists in the listing. Return a pointer to it if
so, NULL if not */
struct State * find(struct State state) {
	short stateId;
	for(stateId = 0; stateId < numStates; stateId++) {
		if(equal(state, states[stateId]) == 1) {
			return &states[stateId];
		}
	}
	return NULL;
}

/* Do the two indicated states overlap (1) or not (0)? */
short overlap(struct State * statePtr1, struct State * statePtr2) {
	short y;
	for(y = 0; y < FULLHEIGHT; y++) {
		if(statePtr1->grid[y] & statePtr2->grid[y]) {
			return 1;
		}
	}
	return 0;
}

/* populate the supplied state with all of the others that it overlaps. */
// void populateOverlappers(struct State * statePtr) {
	// statePtr->numOverlaps = 0;
	// short stateId;
	// for(stateId = 0; stateId < numStates; stateId++) {
		// if(overlap(statePtr, &states[stateId])) {
			// if(statePtr->numOverlaps >= MAXOVERLAPS) {
				// printf("Ran out of space for overlaps.");
				// exit(1);
			// }
			// statePtr->overlapPtrs[statePtr->numOverlaps] = &states[stateId];
			// statePtr->numOverlaps++;
		// }
	// }
// }

/* build the complete network of piece location possibilities */
void buildWeb(void) {
	/* new pieces are placed here */
	short xPosStart = ceil((WIDTH - BOXSIZE) / 2);
	short yPosStart = 0;

	struct State state;
	struct State * nextPtr;
	struct State (* control)(struct State);

	short stateId = 0;
	short pieceId = 0;
	short keyId = 0;

	/* populate the "firstOrientationId" array, so we know when to loop back
	when rotating any given piece */
	short orientationId = 0;
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		firstOrientationId[pieceId] = orientationId;
		orientationId += numOrientations[pieceId];
	}

	/* build an exhaustive list of every possible state, and incidentally
	populate all the "next" pointers, forming an array "states" of NUMPIECES
	separate directed graphs of piece States. */
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		/* start the list */
		state = constructState(pieceId, firstOrientationId[pieceId], xPosStart, yPosStart);
		nextPtr = find(state);
		if(nextPtr == NULL) {
			if(numStates == MAXSTATES) {
				printf("Ran out of space for states.\n");
				exit(1);
			}
			states[numStates] = state;
			nextPtr = &states[numStates];
			numStates++;

			/* store for future reference */
			startPtrs[pieceId] = nextPtr;
		}

		/* iterate over a growing list */
		while(stateId < numStates) {
			/* try every possible key operation in turn, generating pointers. */
			for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
				control = controls[keyId];
				state = control(states[stateId]);
				nextPtr = find(state);

				/* If state is new, it should be put into the array for future
				reference. */
				if(nextPtr == NULL) {
					if(numStates == MAXSTATES) {
						printf("Ran out of space for states.\n");
						exit(1);
					}
					states[numStates] = state;
					nextPtr = &states[numStates];
					numStates++;
				}

				/* record this pointer. */
				states[stateId].nextPtrs[keyId] = nextPtr;
			}

			stateId++;
		}
	}

	/* populate each State with all of the other states which it overlaps. */
	// for(stateId = 0; stateId < numStates; stateId++) {
		// populateOverlappers(&states[stateId]);
	// }
}

/* is there room in this well for a piece in this state (0) or is there an
obstruction (1) ? */
short collision(struct State * statePtr) {
	short y;
	for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
		if(well[y] & statePtr->grid[y]) {
			return 1;
		}
	}
	return 0;
}

short depth = 0;
struct State * getMagicLandingPtr(struct State *);
short getKillerPiece();
struct State * landingPtrs[MAXDEPTH][MAXSTATES];

/* given where we are currently, find all reachable legitimate (i.e. non-deadly)
landing sites, recursively. If a line can be made here, return a pointer to the
landing state where this happens. If not, populate the full list and return
NULL */
struct State * getLinePtr(short * numLandingsPtr, struct State * statePtr) {
	short keyId, y;

	/* this state is reachable... */
	statePtr->reachable = 1;

	/* and investigate the rest */
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {

		/* can't move down from here and not dead? it's a landing site */
		if(
			   controls[keyId] == down
			&& (
				   statePtr->nextPtrs[keyId] == statePtr
				|| collision(statePtr->nextPtrs[keyId]) == 1
			)
			&& statePtr->yTop >= BOXSIZE
		) {

			/* has a line been made? */
			for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
				/* a line has been made! */
				if((well[y] | statePtr->grid[y]) == LINETRIGGER) {
					// show(statePtr);
					// printf("You can get a line from this position with this piece\n");
					// getchar();
					return statePtr;
				}
			}

			/* no line, boring */
			landingPtrs[depth][*numLandingsPtr] = statePtr;
			(*numLandingsPtr)++;
		}

		/* what locations could be next? */
		if(
			   statePtr->nextPtrs[keyId] != statePtr
			&& collision(statePtr->nextPtrs[keyId]) == 0
			&& (statePtr->nextPtrs[keyId])->reachable == 0
		) {
			struct State * linePtr = getLinePtr(numLandingsPtr, statePtr->nextPtrs[keyId]);
			if(linePtr != NULL) {
				return linePtr;
			}
		}
	}
	return NULL;
}

/* Find a landing state where landing the current piece would result in an
immediate line (or, if the player plays rationally afterwards, an eventual
guaranteed line) and return a pointer to that state. If no such state can be
found, return NULL. */
struct State * getMagicLandingPtr(struct State * statePtr) {
	short stateId, j, y, numLandings = 0;

	// show(statePtr);
	// printf("Finding the rational landing state for this piece\n");
	// getchar();

	/* assume everything is unreachable */
	for(stateId = 0; stateId < numStates; stateId++) {
		states[stateId].reachable = 0;
	}

	/* explore */
	struct State * magicLandingPtr = getLinePtr(&numLandings, statePtr);
	if(magicLandingPtr != NULL) {
		return magicLandingPtr;
	}

	/* iterate over all landing sites */
	for(stateId = 0; stateId < numLandings; stateId++) {

		// show(landingPtrs[depth][stateId]);
		// printf("Landing site %d of %d\n", stateId+1, numLandings);
		// getchar();

		/* add the piece to the well */
		/* check out the high optimisation here */
		for(y = landingPtrs[depth][stateId]->yTop; y < landingPtrs[depth][stateId]->yBottom; y++) {
			well[y] |= landingPtrs[depth][stateId]->grid[y];
		}

		/* recurse in this new well state */
		depth++;
		if(getKillerPiece() == -1) {
			depth--;

			/* undo that well modification */
			for(y--; y >= landingPtrs[depth][stateId]->yTop; y--) {
				well[y] &= ~landingPtrs[depth][stateId]->grid[y];
			}

			return landingPtrs[depth][stateId];
		}

		depth--;

		/* undo that well modification */
		for(y--; y >= landingPtrs[depth][stateId]->yTop; y--) {
			well[y] &= ~landingPtrs[depth][stateId]->grid[y];
		}
	}

	/* if we reach this stage then there is no way that a player can force a
	line after this point - the player dies with no lines. */
	// if(depth == 0) {
		// show(statePtr);
		// printf("Depth %d: AI wins if it gives you this piece\n", depth);
		// getchar();
	// }
	return NULL;
}

/* Can a rational player starting from the current well configuration always
force a line? (-1) or can an evil AI always kill you with no lines? (return ID
of killer piece, 0 to 6) */
short getKillerPiece() {
	short pieceId;
	struct State * magiclandingPtrs[NUMPIECES];

	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		magiclandingPtrs[pieceId] = getMagicLandingPtr(startPtrs[pieceId]);
		if(magiclandingPtrs[pieceId] == NULL) {
			return pieceId;
		}
	}

	// if(depth == 0) {
		// for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
			// show(magiclandingPtrs[pieceId]);
			// printf("Depth %d: You can always get a line from this position. If it gives you piece %c, land it here\n", depth, pieceNames[pieceId]);
			// getchar();
		// }
	// }
	return -1;
}

/* generate an exhaustive web of linked possible piece positions and
orientations, then let the user navigate this web */
int main(int argc, char ** argv) {
	short result;
	long start, end;

	/* well initialisation */
	short y;
	for(y = 0; y < FULLHEIGHT; y++) {
		well[y] = 0;
	}

	/* map all possibilities */
	buildWeb();

	/* current well is empty */
	start = time(NULL);
	result = getKillerPiece();
	end = time(NULL);

	show(NULL);
	printf("WIDTH = %d, HEIGHT = %d\n", WIDTH, HEIGHT);
	if(result == -1) {
		printf("You can always get a line\n");
	} else {
		printf("Depth %d: An evil AI can always kill you with no lines from this position, if it gives you piece %c\n", depth, pieceNames[result]);
	}
	printf("Processing took %d second(s)\n", end - start);
	return 0;
}
