#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#define VM_SLOTS       1000
#define NUM_LOE        1
#define BOARD_WIDTH    10000
#define BOARD_HEIGHT   10000
#define NUM_REG        9
#define MAX_ORGANISMS  50000
#define MAX_LOOP_LEVEL 50
#define SEARCH_DIST    25
#define MAX_SIMUL_COLL 25
#define ORG_TO_FOOD    4
#define ORG_LIFESPAN   1000000
#define ORG_REPRODUCE  300
#define ORG_HUNGER     300
#define ORG_FOOD       250

//TODO: Add frameshift and bitwise mutations, organisms can have thread count mutated, etc.

int PAUSE_FROM_STACKOVERFLOW()
{
    int ch;
    struct termios oldt, newt;
    
    tcgetattr ( STDIN_FILENO, &oldt );
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr ( STDIN_FILENO, TCSANOW, &newt );
    ch = getchar();
    tcsetattr ( STDIN_FILENO, TCSANOW, &oldt );
    
    return ch;
}

/* For purposes of printing */
unsigned int columns;
unsigned int rows;

/* What is contained in the environment */
enum environmental_tile {
    ENVIRONMENT_EMPTY,
    ENVIRONMENT_ORGANISM,
    ENVIRONMENT_OBSTACLE,
    ENVIRONMENT_FOOD
};

enum environmental_tile environment[BOARD_WIDTH][BOARD_HEIGHT]; //contains environmental information

void fill_environment() {
    unsigned int x;
    unsigned int y;
    for(x = 0; x < BOARD_WIDTH; x++) {
        for(y = 0; y < BOARD_HEIGHT; y++) {
            int r = rand() % 500;
            if(r < 10) {
                environment[x][y] = ENVIRONMENT_FOOD;
            } else if(r < 15) {
                environment[x][y] = ENVIRONMENT_OBSTACLE;
            }
        }
    }
}

struct location {
    unsigned int x;
    unsigned int y;
};

enum direction {
    DIRECTION_LEFT,
    DIRECTION_RIGHT,
    DIRECTION_UP,
    DIRECTION_DOWN
};

/* One held for each organism's line of execution (LOE) */
struct context_info {
    /* Instruction pointer */
    unsigned int i_ptr;
    
    /* Main pointer */
    unsigned int ptr;

    /* Registers */
    unsigned char reg[NUM_REG];
    
    /* Loop information */
    int loop_level; //depth of loop
    unsigned int prevAddresses[MAX_LOOP_LEVEL]; //start addresses of loops
};

struct organism {
    unsigned int id;
    
    /* Used for managing death and causing hunger */
    unsigned int ticks_since_birth;

    /* Current coordinate position */
    struct location pos;

    /* Width and height change based on grow */
    unsigned int width;
    unsigned int height;

    int food;
    enum direction dir;

    /* Virtual machine bytecodes */
    unsigned char vm[VM_SLOTS];

    struct context_info loe[NUM_LOE];

    /* Shared register */
    unsigned char shared_reg;
};

//CHANGE LATER
struct collision_information_bundle* organism_write_location(struct organism* o);

/* Holds collision data of organism */
struct collision_information {
    enum environmental_tile collidedWith;
    struct organism* org; //NULL unless collided with organism
    struct location pos; //location of item that was collided with
};

/* Used to hold simultaneous collisions */
struct collision_information_bundle {
    unsigned int num;
    struct collision_information collisions[MAX_SIMUL_COLL];
};

/* Randomizes virtual machine bytecodes using VM_SLOTS size. Used before reproduction. */
void randomizeVM(unsigned char* toRead) {
    int i;
    for(i = 0; i < VM_SLOTS; i++) {
        toRead[i] = (unsigned char)rand();
    }
}

/* Current organism max id */
unsigned int max_organism_id = 0;

/* Organisms array */
struct organism* organisms[MAX_ORGANISMS];

void organism_print(struct organism* o) {
    printf("Organism %u:\n", o->id);
    printf("x: %u, y: %u\n", o->pos.x, o->pos.y);
    printf("width: %u, height: %u\n", o->width, o->height);
    printf("food: %u, direction: %u\n", o->food, o->dir);
    printf("Printing 50 VM bytecodes:\n");
    int i;
    for(i = 0; i < 50; i++) {
        printf("%u ", (o->vm)[i]);
    }
    printf("\n");
    for(i = 0; i < NUM_LOE; i++) {
        printf("LOE #%d:\n", i);
        printf("  IP: %u\n", o->loe[i].i_ptr);
        printf("  *IP: %u\n", o->vm[o->loe[i].i_ptr]);
        printf("  P: %u\n", o->loe[i].ptr);
        printf("  *P: %u\n", o->vm[o->loe[i].ptr]);
        int p;
        for(p = 0; p < NUM_REG; p++) {
            printf("  Register %d: %u\n", p, o->loe[i].reg[p]);
        }
    }
}

/* Performs linear search of the array to find an empty slot */
unsigned int next_organism_id() {
    int i;
    for(i = 0; i < MAX_ORGANISMS; i++) {
        if(organisms[i] == NULL) {
            return i;
        }
    }
    return MAX_ORGANISMS+1;
}

/* Deletes an organism */
void organism_delete(struct organism* org, int reason) {
    organisms[org->id] = NULL;
    printf("Organism %d died for reason %d\n", org->id, reason);
    printf("---------DUMPING DELETE DATA-----------\n");
    organism_print(org);
    printf("---------DUMPING VM--------------------\n");
    unsigned int i;
    for(i = 0; i < VM_SLOTS; i++) {
        printf("%d ", org->vm[i]);
    }
    printf("\n---------END DUMPING VM----------------\n");
    printf("---------END DUMPING DELETE DATA-------\n");
    free(org);
}

struct organism* organism_factory() {
    unsigned int new_id = next_organism_id();
    if(new_id > max_organism_id) {
        max_organism_id = new_id;
    }
    if(new_id > MAX_ORGANISMS) {
        /* No available slots */
        printf("No organism slots available!\n");
        return NULL;
    }
    
    /* Create and initialize ID */
    struct organism* new_org = malloc(sizeof(struct organism));
    new_org->id = new_id;
    organisms[new_id] = new_org;

    /* Move organism to center */
    new_org->pos.x = BOARD_WIDTH/2;
    new_org->pos.y = BOARD_HEIGHT/2;

    /* Initialize width, height, food, and direction */
    new_org->width  = 1;
    new_org->height = 1;
    new_org->food   = ORG_FOOD;
    new_org->dir    = DIRECTION_UP;
    
    /* Set registers and instruction pointer to 0 on all LOE */
    int i;
    for(i = 0; i < NUM_LOE; i++) {
        new_org->loe[i].i_ptr = 0;
        new_org->loe[i].ptr = 0;
        int o;
        for(o = 0; o < NUM_REG; o++) {
            new_org->loe[i].reg[o] = 0;
        }
    }
    
    /* If running with more than two threads, set to special indices */
    if(NUM_LOE > 2) {
        new_org->loe[1].i_ptr = 500;
        new_org->loe[2].i_ptr = 750;
    }
    
    /* Set ticks */
    new_org->ticks_since_birth = 0;

    /* Randomize VM bits */
    randomizeVM(new_org->vm);
    
    /* Draw organism on environment */
    organism_write_location(new_org);

    /* One last bit of housekeeping... */
    new_org->shared_reg = 0;

    return new_org;
}

/* Program the first organism with simple instructions */
void organism_make_capable(struct organism* o) {
    unsigned char* vm = o->vm;
    
    /* Simple code that lets the organism detect things around it and move */
    /*vm[0]   = 255;
    vm[1]   = 255;
    vm[2]   = 81;
    vm[3]   = 66;
    vm[4]   = 175;
    vm[5]   = 81;
    vm[6]   = 50;
    vm[7]   = 111;
    vm[8]   = 100;
    vm[9]   = 21;
    vm[10]  = 91;*/
    vm[0]   = 255;
    vm[1]   = 255;
    vm[2]   = 81;
    vm[3]   = 105;
    vm[4]   = 85;
    vm[5]   = 194;
    vm[6]   = 165;
    vm[7]   = 85;
    vm[8]   = 55;
    vm[9]   = 32;
    vm[10]  = 95;
    vm[11]  = 65;
    vm[12]  = 95;
    vm[13]  = 175;
    vm[14]  = 85;
    vm[15]  = 75;
    vm[16]  = 195;
    vm[17]  = 165;
    vm[18]  = 85;
    vm[19]  = 55;
    vm[20]  = 32;
    vm[21]  = 95;
    vm[22]  = 175;
    vm[23]  = 95;
    vm[24]  = 225;
    vm[25]  = 85;
    vm[26]  = 65;
    vm[27]  = 225;
    vm[28]  = 95;
    vm[29]  = 25;
    vm[30]  = 95;
    vm[31]  = 25;
    vm[32] =  99;
    
    vm[300] = 205;
    vm[301] = 84;
    vm[302] = 18;
    vm[303] = 95;
    
    vm[500] = 5;
    vm[501] = 85;
    vm[502] = 95;
    
    vm[750] = 5;
    vm[751] = 85;
    vm[752] = 95;
}

/* Returns whether or not an organism collides with a point */
int organism_collides(struct organism* o, struct location pos) {
    return pos.x >= o->pos.x
        && pos.y >= o->pos.y
        && pos.x <= o->pos.x + o->width
        && pos.y <= o->pos.y + o->height;
}

/* Returns an organism that collides with the position */
struct organism* organism_that_collides_with_point(struct location pos) {
    unsigned int i;
    for(i = 0; i < MAX_ORGANISMS; i++) {
        if(organisms[i] == NULL) break;
        if(organism_collides(organisms[i], pos)) {
            return organisms[i];
        }
    }
    return NULL;
}

/*
 * Used as a helper method, returns collision info.
 **************************************************************
 * WARNING: Returned value is malloc'd, make sure to free it! *
 *        (only necessary if collision_check is true)         *
 **************************************************************
*/
struct collision_information_bundle* organism_location_write_helper(struct organism* o, enum environmental_tile toWrite, int collision_check) {
    int startx = o->pos.x - (o->width)/2;
    int starty = o->pos.y - (o->height)/2;
    
    int endx   = o->pos.x + o->width;
    int endy   = o->pos.y + o->height;
    
    int x;
    int y;
     
     if(collision_check) {
         struct collision_information_bundle* ret = malloc(sizeof(struct collision_information_bundle));
         for(x = startx; x < endx; x++) {
             for(y = starty; y < endy; y++) {
                 if(environment[x][y] != ENVIRONMENT_EMPTY) { //collision!
                     struct collision_information* ci = &ret->collisions[ret->num++];
                     ci->collidedWith = environment[x][y];
                     ci->pos.x = x;
                     ci->pos.y = y;
                     if(environment[x][y] == ENVIRONMENT_ORGANISM) {
                         ci->org = organism_that_collides_with_point(ci->pos);
                     }
                 } else {
                     environment[x][y] = toWrite;
                 }
             }
         }
         return ret;
     } else {
         for(x = startx; x < endx; x++) {
             for(y = starty; y < endy; y++) {
                 environment[x][y] = toWrite;
             }
         }
         return NULL;
     }
}

/* Remove an organism's mass from the location array */
void organism_clear_location(struct organism* o) {
    organism_location_write_helper(o, ENVIRONMENT_EMPTY, 0);
}

/* Write an organisms's location to the location array */
struct collision_information_bundle* organism_write_location(struct organism* o) {
    return organism_location_write_helper(o, ENVIRONMENT_ORGANISM, 1);
}

/* Move organism and write changes to array */
struct collision_information_bundle* organism_move(int deltax, int deltay, struct organism* o) {
    organism_clear_location(o);
    o->pos.x += deltax;
    o->pos.y += deltay;
    return organism_write_location(o);
}

/* Genertes delta X and Y from speed and direction */
struct location direction_to_delta(int speed, enum direction dir) {
    struct location ret;
    switch(dir) {
        case DIRECTION_LEFT:
            ret.x = -speed;
            ret.y = 0;
            return ret;
        case DIRECTION_RIGHT:
            ret.x = speed;
            ret.y = 0;
            return ret;
        case DIRECTION_UP:
            ret.x = 0;
            ret.y = -speed;
            return ret;
        case DIRECTION_DOWN:
            ret.x = 0;
            ret.y = speed;
            return ret;
    }
}

/* Moves organism, auto generating delta X and Y based on speed and direction. */
struct collision_information_bundle* organism_move_auto(int speed, enum direction dir, struct organism* o) {
    struct location delta = direction_to_delta(speed, dir);
    return organism_move(delta.x, delta.y, o);
}

/* Finds the square the organism is currently "looking at", based on top left */
struct location organism_looking_at(struct organism* org) {
    /* First, get offset in organism direction */
    struct location delta = direction_to_delta(1, org->dir);
    
    /* Calculate absolute position */
    delta.x += org->pos.x;
    delta.y += org->pos.y;
    
    /* Account for width and height */
    if(org->dir == DIRECTION_RIGHT) {
        delta.x += org->width - 1;
    } else if(org->dir == DIRECTION_DOWN) {
        delta.y += org->height - 1;
    }
    return delta;
}

/* Finds the square the organism is currently "looking at", based on bottom right */
struct location organism_looking_at_end(struct organism* org) {
    /* First, get offset in organism direction */
    struct location delta = direction_to_delta(1, org->dir);
    
    /* Calculate absolute position */
    delta.x += org->pos.x;
    delta.y += org->pos.y;
    
    /* Account for width and height, and make point based on bottom right */
    if(org->dir == DIRECTION_UP) {
        delta.x += org->width - 1;
    } else if(org->dir == DIRECTION_RIGHT) {
        delta.x += org->width - 1;
        delta.y += org->height - 1;
    } else if(org->dir == DIRECTION_DOWN) {
        delta.x += org->width - 1;
        delta.y += org->height - 1;
    } else if(org->dir == DIRECTION_LEFT) {
        delta.y += org->height - 1;
    }
    return delta;
}

struct organism* draw_organism = 0;
void draw_to_console() {
    if(!draw_organism) {
        unsigned int i;
        for(i = 0; i < MAX_ORGANISMS; i++) {
            if(organisms[i] != 0) {
                draw_organism = organisms[i];
            }
        }
        if(i == MAX_ORGANISMS) {
            return;
        }
    }
    char* buff = malloc(columns * rows + 1);
    buff[columns + rows] = 0;
    int startx = draw_organism->pos.x + draw_organism->width/2  - columns/2;
    int starty = draw_organism->pos.y + draw_organism->height/2 - rows/2;
    
    int endx = startx + columns;
    int endy = starty + rows;
    
    int index = 0;
    int x;
    int y;
    for(y = starty; y < endy; y++) {
        for(x = startx; x < endx; x++) {
            switch(environment[x][y]) {
                case ENVIRONMENT_EMPTY:
                    buff[index++] = ' ';
                    break;
                case ENVIRONMENT_OBSTACLE:
                    buff[index++] = 'X';
                    break;
                case ENVIRONMENT_ORGANISM:
                    buff[index++] = 'O';
                    break;
                case ENVIRONMENT_FOOD:
                    buff[index++] = 'F';
                    break;
                default:
                    break;
            }
        }
    }
    printf("%s\n", buff);
    free(buff);
}

enum direction direction_rotate_right(enum direction dir) {
    switch(dir) {
        case DIRECTION_DOWN:
            return DIRECTION_LEFT;
        case DIRECTION_LEFT:
            return DIRECTION_UP;
        case DIRECTION_UP:
            return DIRECTION_RIGHT;
        case DIRECTION_RIGHT:
            return DIRECTION_DOWN;
    }
}

enum direction direction_rotate_left(enum direction dir) {
    switch(dir) {
        case DIRECTION_DOWN:
            return DIRECTION_RIGHT;
        case DIRECTION_RIGHT:
            return DIRECTION_UP;
        case DIRECTION_UP:
            return DIRECTION_LEFT;
        case DIRECTION_LEFT:
            return DIRECTION_DOWN;
    }
}

enum direction direction_inverse(enum direction dir) {
    switch(dir) {
        case DIRECTION_DOWN:
            return DIRECTION_UP;
        case DIRECTION_UP:
            return DIRECTION_DOWN;
        case DIRECTION_LEFT:
            return DIRECTION_RIGHT;
        case DIRECTION_RIGHT:
            return DIRECTION_LEFT;
    }
}

/* Run a specified function a number of times from a starting location, moving forward one tile per time. */
int run_function_in_direction(int(*func)(struct location), struct location loc, enum direction dir, int length)
{
    int destination;
    struct location current;
    int val;
    switch(dir) {
        case DIRECTION_DOWN:
            current.x = loc.x;
            destination = loc.y + length;
            for(current.y = loc.y; current.y < destination && current.y < BOARD_HEIGHT; current.y++) {
                val = func(current);
                if(val) return val;
            }
            break;
        case DIRECTION_UP:
            current.x = loc.x;
            destination = loc.y - length;
            for(current.y = loc.y; current.y > destination && current.y > -BOARD_HEIGHT; current.y--) {
                val = func(current);
                if(val) return val;
            }
            break;
        case DIRECTION_LEFT:
            current.y = loc.y;
            destination = loc.x - length;
            for(current.x = loc.x; current.x > destination && current.x > -BOARD_WIDTH; current.x--) {
                val = func(current);
                if(val) return val;
            }
            break;
        case DIRECTION_RIGHT:
            current.y = loc.y;
            destination = loc.x + length;
            for(current.x = loc.x; current.x < destination && current.x < BOARD_WIDTH; current.x++) {
                val = func(current);
                if(val) return val;
            }
            break;
    }
    return 0;
}

/* Returns organism size */
int organism_size(struct organism* org) {
    return org->width * org->height;
}

/* A function that detects if an organism exists at a location. If it does, it returns its size. Else, 0. */
int organism_size_at_location(struct location l) {
    if(environment[l.x][l.y] == ENVIRONMENT_ORGANISM) {
        struct organism* o = organism_that_collides_with_point(l);
        if(o) {
            return organism_size(o);
        } else {
            return 0;
        }
    }
    return 0;
}

/* A function that tells whether or not the specified location is an obstacle */
int obstacle_exists_at_location(struct location l) {
    if(environment[l.x][l.y] == ENVIRONMENT_OBSTACLE) {
        return 1;
    }
    return 0;
}

/*
 * Repeatedly searches for something, using the function passed in.
 * If exists == 1, return as soon as function returns nonzero value.
 * Otherwise, find max value and return. Returns zero if failed.
 */
int organism_looking_at_searcher(struct organism* org, int(*func)(struct location), int exists) {
    /* First we find the immediate square in which the organism is looking */
    struct location loc = organism_looking_at(org);
    struct location loc2 = organism_looking_at_end(org);
    /*
     * Diagram of above functions if organism->width == organism->height == 4
     * and organism->dir = DIRECTION_LEFT. In this diagram, S == loc and
     * E == loc2. We will loop from S to E in search of something (indicated
     * by arrows).
     *                       ___________________
     *                      |                   |
     *                      |                   |
     *                      |  <--  SOOOO       |
     *                      |  <--   OOOO       |
     *                      |  <--   OOOO       |
     *                      |  <--  EOOOO       |
     *                      |                   |
     *                      |                   |
     *                      |___________________|
     */
    
    /* Now we repeatedly loop checking for organisms in that direction */
    unsigned int* to_modify;
    struct location* loc_being_modified;
    unsigned int max;
    if(loc.x == loc2.x) { //we're looping on y axis
        if(loc.y > loc2.y) { //start from loc2 and increment to loc
            to_modify = &loc2.y;
            max = loc.y;
            loc_being_modified = &loc2;
        } else { //start from loc and increment to loc2
            to_modify = &loc.y;
            max = loc.y;
            loc_being_modified = &loc;
        }
    } else { //we're looping on x axis
        if(loc.x > loc2.x) { //start from loc2 and increment to loc
            to_modify = &loc2.x;
            max = loc.x;
            loc_being_modified = &loc2;
        } else { //start from loc and increment to loc2
            to_modify = &loc.x;
            max = loc2.x;
            loc_being_modified = &loc;
        }
    }
    int result = -1;
    int temp;
    if(exists) { //check if something exists
        while(*to_modify < max) {
            if((result = run_function_in_direction(func, *loc_being_modified, org->dir, SEARCH_DIST))) {
                return result;
            }
            (*to_modify)++;
        }
    } else { //find max
        while(*to_modify < max) {
            temp = run_function_in_direction(func, *loc_being_modified, org->dir, SEARCH_DIST);
            if(temp > result) { //found a bigger value
                result = temp;
            }
            (*to_modify)++;
        }
        return result;
    }
    return 0;
}

/*
 * If an organism is looking at another organism within SEARCH_DIST, return its size.
 * If none, return 0.
 * If multiple organisms, returns size of max.
 */
int organism_looking_at_organism_size(struct organism* org) {
    return organism_looking_at_searcher(org, organism_size_at_location, 0);
}

/* Returns 1 if food exists at location, else 0. */
int food_exists_at_location(struct location l) {
    if(environment[l.x][l.y] == ENVIRONMENT_FOOD) {
        return 1;
    }
    return 0;
}

/* Grow in direction specified by org->dir */
void organism_grow(struct organism* org) {
    switch(org->dir) {
        case DIRECTION_DOWN:
            org->height++;
            break;
        case DIRECTION_UP:
            org->pos.y--;
            org->height++;
            break;
        case DIRECTION_LEFT:
            org->pos.x--;
            org->width++;
            break;
        case DIRECTION_RIGHT:
            org->width++;
            break;
    }
}

/* If an organism exists at location l, remove 1 food from it. Return 1 if successful. */
int fire_upon_organism(struct location l) {
    struct organism* o = organism_that_collides_with_point(l);
    if(o) {
        o->food--;
        return 1;
    }
    return 0;
}

/* Run one bytecode instruction */
struct collision_information_bundle* bytecode_tick(struct organism* org, unsigned int loe_index) {
    /*
     * Cache frequently used vars.
     * Remember, as soon as the below if/else chain executes, the
     * instruction pointer and instructionare no longer valid,
     * because they may have been updated.
     */
    struct context_info* execution_context = &org->loe[loe_index];
    unsigned int i_ptr = execution_context->i_ptr;
    unsigned char instruction = org->vm[i_ptr];
    
    /* Stores any necessary collision information */
    struct collision_information_bundle* collision = NULL;
    
    printf("Organism %d: ", org->id);
    if(instruction <= 10) { //increment pointer
        execution_context->ptr++;
        printf("INC\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 20) { //decrement pointer
        execution_context->ptr--;
        printf("DEC\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 30) { //increment *pointer
        org->vm[execution_context->ptr]++;
        printf("*INC\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 40) { //decrement *pointer
        org->vm[execution_context->ptr]--;
        printf("*DEC\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 50) { //turn right
        org->dir   = direction_rotate_right(org->dir);
        org->food -= 1;
        printf("RIGHT\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 60) { //turn left
        org->dir   = direction_rotate_left(org->dir);
        org->food -= 1;
        printf("LEFT\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 70) { //move forward
        //int speed = (instruction-1) % 10;
        collision = organism_move_auto(1, org->dir, org);
        org->food -= 1;
        printf("FORWARD\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 80) { //move backward
        //int speed = (instruction-1) % 10;
        collision = organism_move_auto(1, direction_inverse(org->dir), org);
        org->food -= 1;
        printf("BACK\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 90) { //while(*ptr > 0) {
        printf("WHILE {");
        if(org->vm[execution_context->ptr] > 0) { //loop condition satisfied
            printf(" SATISFIED");
            organism_print(org);
            //PAUSE_FROM_STACKOVERFLOW();
            if(++(execution_context->loop_level) > MAX_LOOP_LEVEL) { //too many nested loops!
                printf("Too many nested loops on organism %u.\n", org->id);
                execution_context->loop_level--; //restore and do nothing
            } else {
                /*
                 * Save current location so we can jump back to it later if it is
                 * necessary to restart the loop.
                 */
                execution_context->prevAddresses[execution_context->loop_level - 1] = i_ptr;
            }
        } else { //jump to end of loop
            printf("  END");
            /* While not end bracket */
            while(instruction < 91 || instruction > 100) {
                i_ptr++;
                instruction = org->vm[i_ptr];
            }
        }
        printf("\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 100) { //}
        if(execution_context->loop_level > 0) { //we're actually in a loop
            if(org->vm[execution_context->ptr] > 0) { //loop condition satisfied
                execution_context->i_ptr = execution_context->prevAddresses[execution_context->loop_level-1];
            } else { //exit loop
                printf("EXIT_LOOP\n");
                //PAUSE_FROM_STACKOVERFLOW();
                execution_context->loop_level--;
            }
        }
        printf("}\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 110) { //detect creature and save size to ptr
        int size = organism_looking_at_organism_size(org);
        /* Save to *ptr */
        org->vm[execution_context->ptr] = size;
        printf("DETECT\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 120) { //detect creature and then save 1 if exists, or 0 if not
        int organism_exists = organism_looking_at_organism_size(org) == 0 ? 0 : 1;
        org->vm[execution_context->ptr] = organism_exists;
        printf("BIN DETECT\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 130) { //store *ptr in register (instruction-1) % 10
        int reg = (instruction-1) % 10;
        if(reg < NUM_REG) { //store in per-LOE register
            execution_context->reg[reg] = org->vm[execution_context->ptr];
        } else { //store in shared register
            org->shared_reg = org->vm[execution_context->ptr];
        }
        printf("*PTR -> REG %d\n", reg);
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 140) { //store register (instruction-1) % 10 to *ptr
        int reg = (instruction-1) % 10;
        if(reg < NUM_REG) { //store in per-LOE register
            org->vm[execution_context->ptr] = execution_context->reg[reg];
        } else { //store in shared register
            org->vm[execution_context->ptr] = org->shared_reg;
        }
        printf("REG %d -> *ptr\n", reg);
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 150) { //jump to ptr offset
        execution_context->i_ptr += (execution_context->ptr - 128);
        printf("JMP\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 160) { //grow in direction
        organism_grow(org);
        if(org->dir == DIRECTION_LEFT || org->dir == DIRECTION_RIGHT) {
            org->food -= org->height * 15;
        } else {
            org->food -= org->width * 15;
        }
        printf("GROW\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 170) { //if *ptr > 0, *ptr = 1
        if(org->vm[execution_context->ptr] > 0) {
            org->vm[execution_context->ptr] = 1;
        }
        printf("IF *PTR > 0, *PTR = 1\n");
    } else if(instruction <= 180) { //detect obstacle and save 0 or 1 to *ptr
        org->vm[execution_context->ptr] = organism_looking_at_searcher(org, food_exists_at_location, 1);
        printf("DETECT OBSTACLE\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 190) { //store vm[i_ptr+1] in *ptr
        org->vm[execution_context->ptr] = org->vm[execution_context->i_ptr+1];
        printf("vm[i_ptr] -> *ptr\n");
    } else if(instruction <= 200) { //make *ptr random
        org->vm[execution_context->ptr] = (unsigned char)rand();
        printf("Rand -> *ptr\n");
    } else if(instruction <= 210) { //set ptr to i_ptr
        execution_context->ptr = execution_context->i_ptr;
        printf("ptr -> i_ptr\n");
    } else if(instruction <= 220) { //fire; lose energy
        run_function_in_direction(fire_upon_organism, organism_looking_at(org), org->dir, SEARCH_DIST);
        org->food--;
        printf("Fire\n");
    } else if(instruction <= 230) { //detect food ahead, save 0 or 1 to *ptr
        org->vm[execution_context->ptr] = organism_looking_at_searcher(org, food_exists_at_location, 1);
        printf("DETECT FOOD\n");
        //PAUSE_FROM_STACKOVERFLOW();
    } else if(instruction <= 240) { //store current location mod 256 in organism
        org->vm[execution_context->ptr] = (unsigned char)(org->pos.x % 256);
        if(execution_context->ptr+1 < VM_SLOTS) {
            org->vm[execution_context->ptr+1] = (unsigned char)(org->pos.y % 256);
        }
        printf("Store location\n");
    } //otherwise, do nothing
    
    return collision;
}

/* Fixes up VM pointers and returns 0 if organism doesn't have enough food to survive. */
int organism_checkup(struct organism* org) {
    unsigned int loe_index;
    for(loe_index = 0; loe_index < NUM_LOE; loe_index++) {
        struct context_info* execution_context = &org->loe[loe_index];
        if(execution_context->ptr > VM_SLOTS) {
            execution_context->ptr = 0;
        }
        if(execution_context->i_ptr > VM_SLOTS) {
            execution_context->i_ptr = 0;
        }
    }
    return org->food < 0 ? 0 : 1;
}

/* Handle an organism's collision. Returns 1 if organism is now deleted. */
int handle_collision(struct organism* org, struct collision_information* info) {
    if(info->collidedWith == ENVIRONMENT_ORGANISM) { // collided with organism
        if(organism_size(info->org) > organism_size(org)) { //organism collided with is bigger
            org->food += info->org->food;
            org->food += organism_size(info->org) / ORG_TO_FOOD;
            organism_delete(info->org, 1);
            return 0;
        } else { //we're bigger
            info->org->food += org->food;
            info->org->food += organism_size(org) / ORG_TO_FOOD;
            organism_delete(org, 2);
            return 1;
        }
    } else if(info->collidedWith == ENVIRONMENT_OBSTACLE) {
        org->food--;
        if(org->food <= 0) {
            organism_delete(org, 3);
            return 1;
        }
    } else if(info->collidedWith == ENVIRONMENT_FOOD) {
        org->food+=50;
    }
    return 0;
}

/* Perform intentionally lossy copy of organism's VM */
void organism_lossy_copy(struct organism* first, struct organism* second) {
    int i;
    for(i = 0; i < VM_SLOTS; i++) {
        unsigned char c = first->vm[i];
        int r = rand() % 1024;
        /* Perform mutations */
        if(r == 0) { //subtract one
            second->vm[i] = c-1;
        } else if(r == 1) { //add one
            second->vm[i] = c+1;
        } else if(r == 2) { //subtract/add up to 25
            second->vm[i] = (unsigned char)(c+(rand()%25));
        } else if(r == 3) { //completely random instruction
            second->vm[i] = (unsigned char)rand();
        } else if(r == 4) {} //do nothing
        else { //actually copy
            second->vm[i] = c;
        }
    }
}

/* Reproduce and kill organism */
void organism_reproduce(struct organism* org) {
    //Artifical reproduction for now
    //TODO: org->loe[0]->i_ptr = ORG_REPRODUCE;
    printf("Reproduction shall occur, organism: %d\n", org->id);
    if(org->food < ORG_FOOD) { //organism failed at life, delete & abort
        printf("    Failed at life, delete + abort.\n");
        organism_delete(org, 4);
        return;
    }
    int offset = org->width + 15;
    while(org->food > ORG_FOOD/2) {
        printf("    Creating new organism.\n");
        struct organism* o = organism_factory();
        struct location new_location = org->pos;
        new_location.x += offset;
        offset += offset;
        o->pos = new_location;
        o->food += ORG_FOOD*2;
        org->food -= ORG_FOOD*2;
        struct collision_information_bundle* cib = organism_write_location(org);
        if(cib) {
            free(cib);
        }
        organism_lossy_copy(org, o);
        printf("      Organism created:\n------BEGIN PRINT-------");
        organism_print(o);
        printf("---------END ORGANISM PRINT---------\n");
    }
    organism_delete(org, 5);
}

/* Runs each organism threads, does checkups (removing if dead), and performs food ticks */
void organism_loop(struct organism* org) {
    printf("Loop: %d\n", org->id);
    /* Pre bytecode checkup, in case affected by another organism */
    if(!organism_checkup(org)) {
        organism_delete(org, 6);
        return;
    }
    /* Run each LOE in order */
    unsigned int loe_index;
    for(loe_index = 0; loe_index < NUM_LOE; loe_index++) {
        struct collision_information_bundle* collision = bytecode_tick(org, loe_index);
        if(collision) { //the organism collided with something!
            unsigned int i;
            for(i = 0; i < collision->num; i++) {
                if(handle_collision(org, &collision->collisions[i])) {
                    free(collision);
                    return;
                }
            }
            free(collision);
        }
        /* Increment organism's instruction pointer */
        (org->loe[loe_index].i_ptr)++;
    }
    /* Increment organism tick */
    org->ticks_since_birth++;
    /* Check if needs to be hungry */
    if(org->ticks_since_birth % ORG_HUNGER == 0) {
        /* Get hungry! */
        (org->food)--;
    }
    /* Check if needs to die and reproduce */
    if(org->ticks_since_birth > ORG_LIFESPAN) {
        organism_reproduce(org);
    } else {
        /* Post bytecode checkup, in case organism died while running */
        if(!organism_checkup(org)) {
            draw_to_console();
            organism_print(org);
            organism_delete(org, 7);
            return;
        }
    }
    
    //debug
    //organism_print(org);
    //PAUSE_FROM_STACKOVERFLOW();
}

/* Main loop function that runs each organisms's bytecode. Returns 0 if all dead. */
int main_loop() {
    int organisms_exist = 0;
    unsigned int i;
    for(i = 0; i < max_organism_id+1; i++) {
        if(organisms[i] != NULL) {
            organisms_exist = 1;
            organism_loop(organisms[i]);
        }
    }
    return organisms_exist;
}

int main(int argc, char** argv) {
    srand(time(NULL)); //seed the random generator
    
    /* Set up terminal width and height (non-portable) */
    columns = atoi(getenv("COLUMNS"));
    rows    = atoi(getenv("LINES"));
    
    /* Set organisms array to be null */
    memset(organisms, 0, sizeof(struct organism*) * MAX_ORGANISMS);
    
    /* Fill environment with randomly generated things */
    fill_environment();
    
    /*struct organism* test = */organism_factory();
    //organism_make_capable(test);
    while(main_loop()) {
        //draw_to_console();
        //organism_print(test);
        ////PAUSE_FROM_STACKOVERFLOW();
    }
    printf("Everybody died.\n");
    return 0;
}
