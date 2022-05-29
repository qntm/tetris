#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

/******************************************************************************/
// Stage 1: pieces, piece orientations, piece States and the Seven Webs

#define NUMPIECES 7
#define NUMORIENTATIONS 19
#define NUMCONTROLS 4
#define BOXSIZE 4

#define WIDTH 10
#define HEIGHT 4

#define FULLHEIGHT (BOXSIZE + HEIGHT)

#define CACHESOLUTIONS 0

/* Define our piece orientations. This is the right-handed Nintendo Rotation
System with a raised horizontal I piece, also known as the Original Rotation
System. There are no wall or floor kicks, for optimisation reasons.
http://tetris.wikia.com/wiki/ORS */
char firstOrientationId[NUMPIECES];
char numOrientations[NUMPIECES] = {1, 2, 4, 4, 2, 4, 2};
short allMasks[NUMORIENTATIONS][BOXSIZE] = {
	{0, 6, 6, 0}, /* O */
	{0,15, 0, 0}, /* I */
	{2, 2, 2, 2}, /* I */
	{0,14, 2, 0}, /* J */
	{4, 4,12, 0}, /* J */
	{8,14, 0, 0}, /* J */
	{6, 4, 4, 0}, /* J */
	{0, 7, 4, 0}, /* L */
	{6, 2, 2, 0}, /* L */
	{1, 7, 0, 0}, /* L */
	{2, 2, 3, 0}, /* L */
	{0, 3, 6, 0}, /* S */
	{2, 3, 1, 0}, /* S */
	{0, 7, 2, 0}, /* T */
	{2, 6, 2, 0}, /* T */
	{2, 7, 0, 0}, /* T */
	{2, 3, 2, 0}, /* T */
	{0, 6, 3, 0}, /* Z */
	{1, 3, 2, 0}  /* Z */
};

/* There is a static collection of only about 4000 possible positions and
orientations that a piece can hold in the well. We generate a complete list
of these. Each one contains pointers to all the "next" states obtained by pressing
a key at this point, as well as containing as much pre-calculated data as is practical. */
struct State {
	/* irritatingly, every state must know what piece it is, and its
	orientation */
	char pieceId;
	char orientationId;

	/* number of grid squares of gap between the top right corner of the well
	and the top right corner of the 4x4 bounding box of every state */
	short xPos;
	short yPos;

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

	/* is this state currently reachable (1) or not (0)? */
	short reachable;

	/* What's next in this linked list, if anything? (NULL = end) */
	struct State * successor;
};

/* Linked list of all possible states. */
struct State * states = NULL;

/* list of states where new pieces are instantiated. */
struct State * startPtrs[NUMPIECES];

/* Constructor requires only a piece ID, orientation ID, xPos and yPos.
Everything else is generated from these. */
struct State State(char pieceId, char orientationId, short xPos, short yPos) {
	short maskId = 0;
	short allMask = 0;
	char keyId = 0;
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

	/* Linked list stuffs */
	state.successor = NULL;

	return state;
}

/* return the state after R key */
struct State right(struct State state) {
	/* can't shift off the right side of the screen */
	if(state.xPos + state.xOffset <= 0) {
		return state;
	}

	/* success */
	return State(state.pieceId, state.orientationId, state.xPos - 1, state.yPos);
}

/* return the state after L key */
struct State left(struct State state) {
	/* can't shift off the left side of the screen */
	if(state.xPos + state.xOffset + state.xDim >= WIDTH) {
		return state;
	}

	/* success */
	return State(state.pieceId, state.orientationId, state.xPos + 1, state.yPos);
}

/* try to move piece down */
struct State down(struct State state) {
	/* can't shift below the bottom of the well */
	if(state.yPos + state.yOffset + state.yDim >= FULLHEIGHT) {
		return state;
	}

	/* success */
	return State(state.pieceId, state.orientationId, state.xPos, state.yPos + 1);
}

/* try to rotate the piece */
struct State rotate(struct State state) {
	struct State newP;

	char newOrientationId = state.orientationId + 1;
	if(newOrientationId >= firstOrientationId[state.pieceId] + numOrientations[state.pieceId]) {
		newOrientationId = firstOrientationId[state.pieceId];
	}

	newP = State(state.pieceId, newOrientationId, state.xPos, state.yPos);

	/* check boundaries */
	if(
		   newP.xPos + newP.xOffset < 0
		|| newP.xPos + newP.xOffset + newP.xDim > WIDTH
		|| newP.yPos + newP.yOffset < 0
		|| newP.yPos + newP.yOffset + newP.yDim > FULLHEIGHT
	) {
		return state;
	}

	return newP;
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

/* compare two States to avoid duplicates in the listing. The only
distinguishing features are piece ID, orientation, xPos and yPos.
Everything else is derived. */
short sameState(struct State p1, struct State p2) {
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

/* See if a State already exists in memory. Return a pointer to it if
so, NULL if not */
struct State * locateState(struct State state) {
	struct State * statePtr = states;
	while(statePtr != NULL) {
		if(sameState(state, *statePtr) == 1) {
			return statePtr;
		}
		statePtr = statePtr->successor;
	}
	return NULL;
}

/* Store a State in memory and return a pointer to it. */
struct State * storeState(struct State state) {
	struct State * newPtr = malloc(sizeof(struct State));

	/* Is there room? */
	if(newPtr == NULL) {
		printf("Ran out of memory.\n");
		exit(1);
	}

	*newPtr = state;

	/* List is empty, must be initiated */
	if(states == NULL) {
		states = newPtr;
	}

	/* List has elements, must find the end of it */
	else {
		struct State * statePtr = states;
		while(statePtr->successor != NULL) {
			statePtr = statePtr->successor;
		}
		statePtr->successor = newPtr;
	}

	return newPtr;
}

/* Look up the supplied state in memory and return a pointer to its location
in memory. If it cannot be found, store it and return the pointer to where it
was stored. */
struct State * getStatePtr(struct State state) {
	struct State * statePtr = locateState(state);
	if(statePtr == NULL) {
		statePtr = storeState(state);
	}
	return statePtr;
}

/* build the complete network of piece location possibilities */
void buildWeb(void) {
	/* populate the "firstOrientationId" array, so we know when to loop back
	when rotating any given piece */
	char pieceId = 0;
	char orientationId = 0;
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		firstOrientationId[pieceId] = orientationId;
		orientationId += numOrientations[pieceId];
	}

	/* new pieces are placed here */
	short xPosStart = ceil((WIDTH - BOXSIZE) / 2);
	short yPosStart = 0;

	short stateId = 0;
	char keyId = 0;

	/* build an exhaustive list of every possible state, and incidentally
	populate all the "next" pointers, forming an array "states" of NUMPIECES
	separate directed graphs of piece States. */
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {

		/* start the list */
		struct State startState = State(pieceId, firstOrientationId[pieceId], xPosStart, yPosStart);
		struct State * startPtr = getStatePtr(startState);

		/* store for future reference */
		startPtrs[pieceId] = startPtr;

		/* iterate over a growing list */
		struct State * statePtr = states;
		while(statePtr != NULL) {

			/* try every possibly key operation in turn, generating pointers. */
			for(keyId = 0; keyId < NUMCONTROLS; keyId++) {

				/* What's next in this direction? */
				struct State nextState = (controls[keyId])(*statePtr);

				/* If nextState is new, it should be put into the array for future
				reference. */
				struct State * nextPtr = getStatePtr(nextState);

				/* record this pointer. */
				statePtr->nextPtrs[keyId] = nextPtr;
			}

			statePtr = statePtr->successor;
		}
	}
}

/******************************************************************************/
// Stage 2A: caching of solutions for rapid lookup

#define LINETRIGGER ((1 << WIDTH) - 1)

// Who wins?
#define AI 0
#define PLAYER 1
#define UNKNOWN 2

/* Stores who wins, and what their winning strategy is */
struct Solution {
	char whoWins;
	union {
		char killerPieceId;
		struct State * magicLandings[NUMPIECES];
	} strat;
};

struct Solution Solution(char whoWins, char killerPieceId, struct State ** magicLandings) {
	struct Solution solution;

	/* who wins? */
	solution.whoWins = whoWins;

	/* And how? */
	if(whoWins == AI) {
		solution.strat.killerPieceId = killerPieceId;
	}

	if(whoWins == PLAYER) {
		char pieceId;
		for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
			solution.strat.magicLandings[pieceId] = magicLandings[pieceId];
		}
	}

	return solution;
}

/* Rudimentary constant-time lookup table for well solutions. */
struct Node {
	union {
		struct Node * nodePtr;
		struct Solution * solutionPtr;
	} children[LINETRIGGER];
};

/* Place from which all wells originate. Has to be populated at run time */
struct Node * rootNodePtr = NULL;

/* Store a new empty Node with all null pointers in memory, and return a
pointer to the location of this Node in memory */
struct Node * storeNode() {
	struct Node * nodePtr = malloc(sizeof(struct Node));

	/* Is there room? */
	if(nodePtr == NULL) {
		printf("Ran out of memory.\n");
		exit(1);
	}

	short b;
	for(b = 0; b < LINETRIGGER; b++) {
		nodePtr->children[b].nodePtr = NULL;
	}
	return nodePtr;
}

/* 1 if same, 0 if different */
short sameWell(short * w1, short * w2) {
	short y;
	for(y = FULLHEIGHT - 1; y >= 0; y--) {
		if(w1[y] != w2[y]) {
			return 0;
		}
	}
	return 1;
}

/* See if a Solution to the supplied well already exists in memory. Return
who wins: AI, PLAYER or UNKNOWN. */
char getCachedWinner(short * well) {
	struct Node * nodePtr = rootNodePtr;
	short y;

	for(y = 0; y < FULLHEIGHT - 1; y++) {
		/* intermediate layer missing */
		if(nodePtr->children[well[y]].nodePtr == NULL) {
			return UNKNOWN;
		}
		nodePtr = nodePtr->children[well[y]].nodePtr;
	}

	/* all layers present: no solution: UNKNOWN */
	if(nodePtr->children[well[FULLHEIGHT - 1]].solutionPtr == NULL) {
		return UNKNOWN;
	}

	return nodePtr->children[well[FULLHEIGHT - 1]].solutionPtr->whoWins;
}

/* Store the solution to the supplied well in memory and return a pointer to
its new location. */
struct Solution * cacheSolution(short * well, struct Solution solution) {

	/* First, save the solution. */
	struct Solution * solutionPtr = malloc(sizeof(struct Solution));

	/* Is there room? */
	if(solutionPtr == NULL) {
		printf("Ran out of memory.\n");
		exit(1);
	}

	*solutionPtr = solution;

	/* Then save the well result. */
	struct Node * nodePtr = rootNodePtr;
	short y;
	for(y = 0; y < FULLHEIGHT - 1; y++) {
		if(nodePtr->children[well[y]].nodePtr == NULL) {
			nodePtr->children[well[y]].nodePtr = storeNode();
		}
		nodePtr = nodePtr->children[well[y]].nodePtr;
	}
	nodePtr->children[well[FULLHEIGHT - 1]].solutionPtr = solutionPtr;

	return solutionPtr;
}

/******************************************************************************/
// Stage 2B: actually computing all the reachable wells and Solutions for each

/* is there room in this well for a piece in this state (0) or is there an
obstruction (1) ? */
short collision(short * well, struct State * statePtr) {
	short y;
	for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
		if(well[y] & statePtr->grid[y]) {
			return 1;
		}
	}
	return 0;
}

/* Encapsulates a pointer to a State which constitutes a landing location */
struct Landing {
	/* Pointer to the landing State */
	struct State * statePtr;

	/* Successor in this linked list */
	struct Landing * successor;
};

/* Constructor for a Landing */
struct Landing Landing(struct State * statePtr) {
	struct Landing landing;
	landing.statePtr = statePtr;
	landing.successor = NULL;
	return landing;
}

/* given where we are currently, find all reachable legitimate (i.e. non-deadly)
landing sites, recursively. If a line can be made here, return a pointer to the
landing state where this happens. If not, populate the full list and return
NULL */
struct State * getLinePtr(short * well,	struct State * statePtr, struct Landing ** attachPointLoc) {
	char keyId;
	short y;
	struct State * nextStatePtr = NULL;

	/* this state is reachable... */
	statePtr->reachable = 1;

	/* and investigate the rest */
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		nextStatePtr = statePtr->nextPtrs[keyId];

		/* can't move down from here and not dead? it's a landing site */
		if(
			   controls[keyId] == down
			&& statePtr->yTop >= BOXSIZE
			&& (
				   nextStatePtr == statePtr
				|| collision(well, nextStatePtr) == 1
			)
		) {

			/* has a line been made? */
			for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
				/* a line has been made! */
				if((well[y] | statePtr->grid[y]) == LINETRIGGER) {
					return statePtr;
				}
			}

			/* no line, boring. Save this landing state to memory. */

			/* Make a space in memory for this landing state */
			struct Landing * newPtr = malloc(sizeof(struct Landing));

			/* Is there room? */
			if(newPtr == NULL) {
				printf("Ran out of memory.\n");
				exit(1);
			}

			/* Put this landing into that space in memory */
			*newPtr = Landing(statePtr);

			/* attach it to the attach point */
			(*attachPointLoc)->successor = newPtr;

			/* new attach point is next link down the chain */
			*attachPointLoc = newPtr;
		}

		/* what locations could be next? */
		if(
			   nextStatePtr->reachable == 0
			&& collision(well, nextStatePtr) == 0
			// && nextStatePtr != statePtr // statePtr was marked reachable just above
		) {
			struct State * linePtr = getLinePtr(well, nextStatePtr, attachPointLoc);
			if(linePtr != NULL) {
				return linePtr;
			}
		}
	}

	// If we get all the way here then no line can be made using this piece
	return NULL;
}

struct State * getMagicLanding(short *, char);
char solve(short *);

void freeLandings(struct Landing * landing) {
	while(landing != NULL) {
		struct Landing * successor = landing->successor;
		free(landing);
		landing = successor;
	}
}

void show(short *, struct State *);

/* Find a landing state where landing the current piece would result in an
immediate line (or, if the player plays rationally afterwards, an eventual
guaranteed line) and return a pointer to that state. If no such state can be
found, return NULL. */
struct State * getMagicLanding(short * well, char pieceId) {

	/* assume everything is unreachable */
	struct State * statePtr = states;
	while(statePtr != NULL) {
		statePtr->reachable = 0;
		statePtr = statePtr->successor;
	}

	/* Origin point for a linked list of Landings. This first entry doesn't count,
	it just serves as an anchor for more. Having a dummy first entry causes
	substantially fewer headaches */
	struct Landing anchor = Landing(NULL);

	/* In order for getLinePtr() to be able to expand the linked list, it must
	know where to find it */
	struct Landing * attachPoint = &anchor;

	/* Is it possible to get an immediate line? If so, return that landing
	state */
	struct State * magicLanding = getLinePtr(well, startPtrs[pieceId], &attachPoint);
	if(magicLanding != NULL) {
		freeLandings(&anchor);
		return magicLanding;
	}

	/* Otherwise, iterate over all landing sites looking for further strategy */
	short stateId = 0;
	short y = 0;
	struct Landing * landingPtr = anchor.successor; // skip the first one
	while(landingPtr != NULL) {
		statePtr = landingPtr->statePtr;

		// show(well, statePtr);
		// getchar();

		/* add the piece to the well */
		/* check out the high optimisation here */
		for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
			well[y] |= statePtr->grid[y];
		}

		char whoWins = solve(well);

		/* undo that well modification */
		for(y = statePtr->yBottom-1; y >= statePtr->yTop; y--) {
			well[y] &= ~statePtr->grid[y];
		}

		/* PLAYER wins? This landing position is magic! Return it! */
		if(whoWins == PLAYER) {
			freeLandings(&anchor);
			return statePtr;
		}

		/* AI wins: continue */

		landingPtr = landingPtr->successor;
	}

	/* if we reach this stage then there is no way that a player can force a
	line after this point - the player dies with no lines. */
	freeLandings(&anchor);
	return NULL;
}

/* Supplied with a well, either look up the Solution that has already been
calculated for this well, or compute it from scratch. A Solution explains who
can force a win and what their strategy is. Return a pointer to the Solution's
location in memory. */
char solve(short * well) {

#if CACHESOLUTIONS == 1
	/* Look up the result from this well in the memo table. */
	char whoWins = getCachedWinner(well);

	/* Success! Return pointer to the result */
	if(whoWins != UNKNOWN) {
		return whoWins;
	}
#endif

	/* Failure? Too bad, have to calculate the result manually and save it
	ourselves */

	char pieceId;
	struct State * magicLandings[NUMPIECES];
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		magicLandings[pieceId] = getMagicLanding(well, pieceId);

		if(magicLandings[pieceId] == NULL) {

			/* AI wins by supplying this piece */
#if CACHESOLUTIONS == 1
			cacheSolution(well, Solution(AI, pieceId, NULL));
#endif

			return AI;
		}
	}

	/* PLAYER wins by landing each piece in the magic location */
#if CACHESOLUTIONS == 1
	cacheSolution(well, Solution(PLAYER, -1, magicLandings));
#endif

	return PLAYER;
}

/******************************************************************************/
// Stage 3: Printing stuff out

char pieceNames[NUMPIECES] = {'O', 'I', 'J', 'L', 'S', 'T', 'Z'};

/* show details about this state */
void print(struct State * statePtr) {
	short y;
	char keyId;
	printf("I am piece %c in orientation %d @ %p\n", pieceNames[statePtr->pieceId], statePtr->orientationId, statePtr);
	printf("(xPos, yPos) is (%d, %d)\n", statePtr->xPos, statePtr->yPos);
	printf("(xOffset, yOffset) is (%d, %d)\n", statePtr->xOffset, statePtr->yOffset);
	printf("(xDim, yDim) is (%d, %d)\n", statePtr->xDim, statePtr->yDim);
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		printf("nextPtrs[%d] is %p\n", keyId, statePtr->nextPtrs[keyId]);
	}
	for(y = statePtr->yTop; y < statePtr->yBottom; y++) {
		printf("grid[%d] is %d\n", y, statePtr->grid[y]);
	}
	return;
}

/* show the state as it appears in the well */
void show(short * well, struct State * statePtr) {
	short x, y;

	system("cls");
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

/* Show who wins a given well, and how. */
void showSolution(short * well, struct Solution * solutionPtr) {

	/* PLAYER wins */
	if(solutionPtr->whoWins == PLAYER) {
		char pieceId;
		for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
			show(well, solutionPtr->strat.magicLandings[pieceId]);
			printf("If the AI gives you piece %c, land it here\n", pieceNames[pieceId]);
			getchar();
		}
	}

	/* AI wins */
	if(solutionPtr->whoWins == AI) {
		show(well, NULL);
		printf("An evil AI can always kill you with no lines from this position, if it gives you piece %c\n", pieceNames[solutionPtr->strat.killerPieceId]);
		getchar();
	}

	return;
}

/* Count all the Solutions that were computed */
long countSolutions(struct Node * nodePtr, short y) {
	long numWells = 0;
	short b;
	for(b = 0; b < LINETRIGGER; b++) {
		if(y == FULLHEIGHT - 1) {
			if(nodePtr->children[b].solutionPtr != NULL) {
				numWells++;
			}
		}	else {
			if(nodePtr->children[b].nodePtr != NULL) {
				numWells += countSolutions(nodePtr->children[b].nodePtr, y+1);
			}
		}
	}

	return numWells;
}

/* show all the Solutions that were computed (this is magical recursive fun) */
void showSolutions(short * well, struct Node * nodePtr, short y) {

	short b;
	for(b = 0; b < LINETRIGGER; b++) {
		well[y] = b;

		/* We're at the leaf nodes of the tree: well is fully populated:
		print it! */
		if(y == FULLHEIGHT - 1) {
			if(nodePtr->children[b].solutionPtr != NULL) {
				showSolution(well, nodePtr->children[b].solutionPtr);
			}
		}

		/* Recurse into the next layer of trees */
		else {
			if(nodePtr->children[b].nodePtr != NULL) {
				showSolutions(well, nodePtr->children[b].nodePtr, y+1);
			}
		}
	}

	return;
}

void showAllSolutions() {
	printf("Number of wells: %d\n", countSolutions(rootNodePtr, 0));
	getchar();

	/* Show all well results */
	short well[FULLHEIGHT];
	short y;
	for(y = 0; y < FULLHEIGHT; y++) {
		well[y] = 0;
	}
	showSolutions(well, rootNodePtr, 0);
}

/******************************************************************************/

/* generate an exhaustive web of linked possible piece positions and
orientations, then let the PLAYER navigate this web */
int main(void) {

	/* map all possibilities */
	buildWeb();

	rootNodePtr = storeNode();

	/* Initialise well to emptiness state */
	short well[FULLHEIGHT];
	short y;
	for(y = 0; y < FULLHEIGHT; y++) {
		well[y] = 0;
	}

	/* solve the empty well */
	long start = time(NULL);
	printf("Execution starting at %s\n", asctime(localtime(&start)));
	char whoWins = solve(well);
	long end = time(NULL);

	/* Output */
	printf("WIDTH = %d, HEIGHT = %d\n", WIDTH, HEIGHT);
	if(whoWins == AI    ) { printf("AI wins\n"    ); }
	if(whoWins == PLAYER) { printf("PLAYER wins\n"); }
	printf("Processing took %d second(s)\n", end - start);
	getchar();


#if CACHESOLUTIONS == 1
	/* Show all well results */
	showAllSolutions();
#endif

	return 0;
}
