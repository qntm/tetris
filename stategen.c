/* This file generates full lists of all possible states of all possible pieces.
Separating this procedure out means that the actual search can be easily run by
forked processes without this overhead every time. This procedure is not optimised
because there is no real need to do so.
We also print out some useful information. */

#include "core.c"

/* set to 1 to be shown each state when it is generated */
#define SHOW 0

/* Bounding box on all pieces is a 4x4 box */
#define BOXSIZE 4

/* Centralisation correction for the variable width of the well */
#define MARGIN (short)floor((WIDTH - BOXSIZE) / 2.0)

/* Define our piece orientations. This is the right-handed Nintendo Rotation
System with a raised horizontal I piece, also known as the Original Rotation
System. There are no wall or floor kicks, for optimisation reasons.
http://tetris.wikia.com/wiki/ORS */
short oMasks[][BOXSIZE] = {
	{0, 6, 6, 0}
};
short iMasks[][BOXSIZE] = {
	{0,15, 0, 0},
	{2, 2, 2, 2}
};
short jMasks[][BOXSIZE] = {
	{8,14, 0, 0},
	{6, 4, 4, 0},
	{0,14, 2, 0},
	{4, 4,12, 0}
};
short lMasks[][BOXSIZE] = {
	{1, 7, 0, 0},
	{2, 2, 3, 0},
	{0, 7, 4, 0},
	{6, 2, 2, 0}
};
short sMasks[][BOXSIZE] = {
	{0, 3, 6, 0},
	{2, 3, 1, 0}
};
short tMasks[][BOXSIZE] = {
	{2, 7, 0, 0},
	{2, 3, 2, 0},
	{0, 7, 2, 0},
	{2, 6, 2, 0}
};
short zMasks[][BOXSIZE] = {
	{0, 6, 3, 0},
	{1, 3, 2, 0}
};

/* Handy container for various piece-specific attributes */
struct Piece {
	char letter;
	short spawnXPos; /* varies depending on whether the well has odd or even width */
	short spawnYPos;
	char numOrientations;
	
	/* "masks" is the name of a pointer. The pointer is a pointer to an array.
	The number of elements in the array is BOXSIZE. Each element of the array is
	a short. */
	short (* masks)[BOXSIZE];
	
	short numStates;
};

/* Pieces are arranged into a specific order so that the worst pieces are higher in the list,
the better to force bad results early. O in particular is placed first because it has few
possibilities and can be fully tested faster */
struct Piece pieces[NUMPIECES] = {
  /* letter,  spawnXPos,          spawnYPos,       numOrientations,                   masks    numStates */
	{  'o',     WIDTH % 2 ? 1 : 0,  SPARELINES - 3,  sizeof oMasks / sizeof oMasks[0],  oMasks,  0         },
	{  'i',     WIDTH % 2 ? 1 : 0,  SPARELINES - 3,  sizeof iMasks / sizeof iMasks[0],  iMasks,  0         },
	{  'j',     WIDTH % 2 ? 0 : 0,  SPARELINES - 2,  sizeof jMasks / sizeof jMasks[0],  jMasks,  0         },
	{  'l',     WIDTH % 2 ? 1 : 1,  SPARELINES - 2,  sizeof lMasks / sizeof lMasks[0],  lMasks,  0         },
	{  's',     WIDTH % 2 ? 1 : 1,  SPARELINES - 3,  sizeof sMasks / sizeof sMasks[0],  sMasks,  0         },
	{  't',     WIDTH % 2 ? 1 : 1,  SPARELINES - 2,  sizeof tMasks / sizeof tMasks[0],  tMasks,  0         },
	{  'z',     WIDTH % 2 ? 1 : 1,  SPARELINES - 3,  sizeof zMasks / sizeof zMasks[0],  zMasks,  0         }
};

/* There is a static collection of only about 4000 possible positions and
orientations that a piece can hold in the well. We generate a complete list
of these States. Each State contains pointers to all the "next" States obtained by pressing
keys at this point, as well as containing as much pre-calculated data as is practical.
NOTE: the "State" struct here in stategen is different from the "State" struct present in the
generated code. Specifically, the generated code contains MUCH LESS DATA because it is not
necessary to store it all. */
struct State {
	/* every state must know what piece it is, and its orientation */
	short pieceId;
	char orientationId;

	/* number of grid squares of gap between the top right corner of the well	and
	the top right corner of the BOXSIZE-by-BOXSIZE bounding box of every state */
	short xPos;
	short yPos;

	/* number of grid squares of gap between the top right of the BOXSIZE-by-BOXSIZE
	bounding box and where the piece itself begins */
	short xOffset;
	short yOffset;

	/* dimensions of the piece in grid squares */
	short xDim;
	short yDim;

	/* this stuff is all pre-calculated to make landing calculations fast. */
	short yTop;
	short yBottom;
	short grid[FULLHEIGHT];

	/* pointers to other states in memory which would be obtained by taking
	the named actions. Possibly a pointer to self if this action is not
	permitted (e.g. attempt to move off side of screen) */
	struct State * nextPtrs[NUMCONTROLS];

	/* is this State completely above/outside of the visible playing field, such
	that landing here ends the game immediately (1), or not (0)? */
	char outOfBounds;
	
	/* does this State overlap any spawning State, such that the AI could spawn
	a new piece overlapping it in turn (1), or not (0)? */
	char overlapsSpawn;

	/* is this state currently reachable (1) or not (0)? This is the only non-static
	piece of information and will be rewritten many times during execution */
	char reachable;
	
	/* What index in the list am I? 0 = first */
	short i;
	
	/* What's next in this linked list, if anything? (NULL = this is the end) */
	struct State * successor;
};

/* "states" is the name of an array. The array has NUMPIECES entries. Each
entry is a pointer. Each pointer points to a State.
Each State is the beginning of a linked list of all possible States for the
supplied piece. states[pieceId] is a pointer to the first State in the linked
list, which is the spawn State for the piece with that ID.
*/
struct State * states[NUMPIECES] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Constructor requires only a piece ID, orientation ID, xPos and yPos.
Everything else is generated from these. */
struct State State(short pieceId, short orientationId, short xPos, short yPos) {
	short maskId = 0;
	short allMask = 0;
	short * masks = pieces[pieceId].masks[orientationId];

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
	short keyId = 0;
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		state.nextPtrs[keyId] = NULL;
	}

	/* pre-calc stuff */
	state.yTop = state.yPos + state.yOffset;
	state.yBottom = state.yPos + state.yOffset + state.yDim;

	/* this loop is more time-consuming than it needs to be but feh, it is
	done once ever */
	short y = 0;
	short x = 0;
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

	/* Check for being above the threshold */
	state.outOfBounds = state.yBottom <= SPARELINES ? 1 : 0;

	/* Check for overlapping spawn points */
	state.overlapsSpawn = 0;

	/* If this *is* the spawn State for this piece, then it is of course in
	collision with itself */
	if(states[pieceId] == NULL) {
		state.overlapsSpawn = 1;
	}

	/* So this isn't a spawn State... */
	else {

		/* Check for collision with all spawn States */
		short otherPieceId = 0;
		for(otherPieceId = 0; otherPieceId < NUMPIECES; otherPieceId++) {
			struct State * otherSpawnPtr = states[otherPieceId];

			/* Make sure that all the spawn States are populated! */
			if(otherSpawnPtr == NULL) {
				printf("Can't check for spawn-time collisions until all spawn States are populated. Refactor!\n");
				exit(1);
			}
			
			/* Check for collision. */
			for(y = 0; y < FULLHEIGHT; y++) {
				if(state.grid[y] & otherSpawnPtr->grid[y]) {
					state.overlapsSpawn = 1;
				}
			}
		}
	}
	
	state.reachable = 0;

	/* Just get the next number. We'll increment numStates elsewhere if this
	State actually gets saved, which isn't guaranteed. */
	state.i = pieces[pieceId].numStates;
	
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

	char newOrientationId = (state.orientationId + 1) % pieces[state.pieceId].numOrientations;

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
	struct State * statePtr = states[state.pieceId];
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
	if(states[state.pieceId] == NULL) {
		states[state.pieceId] = newPtr;
	}
	
	/* List has elements, must find the end of it */
	else {
		struct State * statePtr = states[state.pieceId];
		while(statePtr->successor != NULL) {
			statePtr = statePtr->successor;
		}
		statePtr->successor = newPtr;
	}

	/* make a note! */
	pieces[state.pieceId].numStates++;

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

/* show details about this state */
void print(struct State state) {

	short y = 0;
	for(y = state.yTop; y < state.yBottom; y++) {
		printf("grid[%d] is %d\n", y, state.grid[y]);
	}

	short keyId = 0;
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		printf("next[%d] is %d\n", keyId, state.nextPtrs[keyId]->i);
	}

	printf("outOfBounds: %s\n",   state.outOfBounds   ? "TRUE" : "FALSE");
	printf("overlapsSpawn: %s\n", state.overlapsSpawn ? "TRUE" : "FALSE");
	return;
}

/* show the state as it appears in the well */
void show(struct State state) {
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
			if(state.grid[y] >> x & 1) {
				printf("@");
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

	print(state);
	getchar();
	return;
}

/* build the complete network of piece location possibilities.
Provide a filename at the command line to output the information to
that file. Otherwise you'll see it in stdout */
int main(int argc, char ** argv) {

	/* build an exhaustive list of every possible state, and incidentally
	populate all the "next" pointers, forming an array "states" of NUMPIECES
	separate directed graphs of piece States. */

	/* Kick off each list to start */
	short pieceId = 0;
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {

		/* start the list */
		struct State startState = State(
			pieceId,
			0, /* orientationId */
			pieces[pieceId].spawnXPos + MARGIN, /* xPos */
			pieces[pieceId].spawnYPos           /* yPos */
		);

		storeState(startState);
	}

	/* Now iterate over a growing list. (Seven times) */
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		struct State * statePtr = states[pieceId];
		while(statePtr != NULL) {

			/* try every possibly key operation in turn, generating pointers. */
			short keyId = 0;
			for(keyId = 0; keyId < NUMCONTROLS; keyId++) {

				/* What's next in this direction? */
				struct State nextState = (controls[keyId])(*statePtr);

				/* If nextState is new, this call puts into the array for future
				reference. */
				struct State * nextPtr = getStatePtr(nextState);

				/* record this pointer. */
				statePtr->nextPtrs[keyId] = nextPtr;
			}

			statePtr = statePtr->successor;
		}
	}

	/* user can follow along if wanted */
	#if SHOW
		for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
			struct State * statePtr = states[pieceId];
			while(statePtr != NULL) {
				show(*statePtr);
				statePtr = statePtr->successor;
			}
		}
	#endif

	/* Now: print it out.
	Surprisingly little information is actually needed */

	/* provide an optional filename at the command line */
	FILE * out = stdout;
	if(argc >= 2) {
		out = fopen(argv[1], "w");
		if(out == NULL) {
			printf("Couldn't open %s\n", argv[1]);
			exit(1);
		}
	}

	/* These preprocessor declarations remind you to rerun stategen
	if you've changed WIDTH and HEIGHT since last time you ran it */
	fprintf(out, "#if WIDTH != %d\n", WIDTH);
	fprintf(out, "\t#error WIDTH != %d, rerun stategen\n", WIDTH);
	fprintf(out, "#endif\n");
	fprintf(out, "\n");

	fprintf(out, "#if HEIGHT != %d\n", HEIGHT);
	fprintf(out, "\t#error HEIGHT != %d, rerun stategen\n", HEIGHT);
	fprintf(out, "#endif\n");
	fprintf(out, "\n");

	/* This is needed to record the pieceId stack. MAXDEPTH is the maximum
	number of pieces which will fit into the well without creating a line,
	and is therefore the maximum depth to which we anticipate needing to search.
	*/
	fprintf(out, "#define MAXDEPTH %d\n", (FULLHEIGHT * (WIDTH - 1)) / 4);
	
	/* THIS records the landing state stack. MAXSTATES is the maximum number
	of possible distinct landing states for any well. It's a massive overestimate
	but never mind */
	short maxStates = 0;
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		if(pieces[pieceId].numStates > maxStates) {
			maxStates = pieces[pieceId].numStates;
		}
	}
	fprintf(out, "#define MAXSTATES %d\n", maxStates);
	fprintf(out, "#define LINETRIGGER %d\n", (1 << WIDTH) - 1);
	fprintf(out, "\n");

	/* struct declaration */
	fprintf(out, "struct State {\n");
	fprintf(out, "\tshort yTop;\n");
	fprintf(out, "\tshort yBottom;\n");
	fprintf(out, "\tshort grid[FULLHEIGHT];\n");
	fprintf(out, "\tint next[NUMCONTROLS];\n");
	fprintf(out, "\tchar outOfBounds;\n");
	fprintf(out, "\tchar overlapsSpawn;\n");
	fprintf(out, "\tchar reachable;\n");
	fprintf(out, "};\n");
	fprintf(out, "\n");

	/* arrays of structs */
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		fprintf(out, "struct State %cStates[] = {\n", pieces[pieceId].letter);
		struct State * statePtr = states[pieceId];
		while(statePtr != NULL) {
			
			fprintf(out, "\t{");
			
			fprintf(out, "%2d, ", statePtr->yTop);
			fprintf(out, "%2d, ", statePtr->yBottom);
			
			fprintf(out, "{");
			short y = 0;
			for(y = 0; y < FULLHEIGHT; y++) {
				fprintf(out, "%4d", statePtr->grid[y]);
				if(y < FULLHEIGHT-1) {
					fprintf(out, ", ");
				}
			}
			fprintf(out, "}, ");
			
			fprintf(out, "{");
			short keyId = 0;
			for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
				fprintf(out, "%3d", statePtr->nextPtrs[keyId]->i);
				if(keyId < NUMCONTROLS-1) {
					fprintf(out, ", ");
				}
			}
			fprintf(out, "}, ");
			
			fprintf(out, "%5s, ", (statePtr->outOfBounds   ? "TRUE" : "FALSE"));
			fprintf(out, "%5s, ", (statePtr->overlapsSpawn ? "TRUE" : "FALSE"));
			fprintf(out, "%5s"  , (statePtr->reachable     ? "TRUE" : "FALSE"));
			fprintf(out, "}");

			if(statePtr->i < pieces[statePtr->pieceId].numStates - 1) {
				fprintf(out, ",");
			}
			fprintf(out, "\n");

			/* NEXT! */
			statePtr = statePtr->successor;
		}
		fprintf(out, "};\n");
		fprintf(out, "\n");
	}

	/* Master array of all states */
	fprintf(out, "struct State * states[NUMPIECES] = {");
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		fprintf(out, "%cStates", pieces[pieceId].letter);
		if(pieceId < NUMPIECES - 1) {
			fprintf(out, ", ");
		}
	}
	fprintf(out, "};\n");
	fprintf(out, "\n");

	/* numStates array */
	fprintf(out, "short numStates[NUMPIECES] = {\n");
	for(pieceId = 0; pieceId < NUMPIECES; pieceId++) {
		fprintf(out, "\tsizeof %cStates / sizeof %cStates[0]", pieces[pieceId].letter, pieces[pieceId].letter);
		if(pieceId < NUMPIECES - 1) {
			fprintf(out, ", ");
		}
		fprintf(out, "\n");
	}
	fprintf(out, "};\n");
	fprintf(out, "\n");

	/* downKeyId */
	short keyId = 0;
	for(keyId = 0; keyId < NUMCONTROLS; keyId++) {
		if(controls[keyId] != down) {
			continue;
		}
		fprintf(out, "char downKeyId = %d;\n", keyId);
		break;
	}
	fprintf(out, "\n");

	fclose(out);

	return 0;
}