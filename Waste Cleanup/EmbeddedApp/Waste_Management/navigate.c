
#include "navigate.h"
// Manages the event where the cybot either hit something or the cliff sensor sensed a hole
// If the target was reached it returns a 1, otherwise it returns a 1
int manage_not_complete(oi_t* sensor, Coordinate interim_coord) {
    uint8_t hole = getHoleTouching(sensor);
    uint8_t target = getTargetTouching(sensor);
    // Set coordinates not accurate because don't know where it stops
    set_cybot_coords(((cybot_pose.xy.x + interim_coord.x) / 2) - 5, ((cybot_pose.xy.y + interim_coord.y) / 2) - 5);
    if (target) {
        return 1;
    } else if (hole) {
        send_hole_point(sensor);
        move_backwards(sensor, 5, &cybot_pose);
        turn_clockwise(sensor, 120);
    } else if (sensor->bumpRight | sensor->bumpLeft) {
        Field field;
        field.size = 1;
        Pillar pillar[1];
        pillar[0].position = cybot_pose;
        pillar[0].radius = 7.0;
        field.pillars= pillar;
        send_field(field);
        move_backwards(sensor, 5, &cybot_pose);
        if (sensor->bumpRight) {
            turn_counter_clockwise(sensor, 80);
        } else {
            turn_clockwise(sensor, 80);
        }
    }
    return 0;
}
// API for parsing commands given from the GUI
// Communication between cybot and GUI are found in https://github.com/ckugel/RoombaController/wiki/How-To-Communicate-With
bumpy receive_and_execute(oi_t* sensor) {
    Move moves[20];
    Routine routine;
    routine = (Routine) {.moves = moves, .amount = 10};
    int i = 0;
    int startRoutine = 0;
    int count = 0;
    char num_buffer[10];
    int buffer_loc = 0;
    int dec = 0;
    int dec_loc = 0;
    while (true) {
        char routine_str = uart_receive();
        // First R means beginning of routine
        if (routine_str == 'R' && !startRoutine) {
            startRoutine = 1;
        }
        // Second R means the end of the routine
        else if (routine_str == 'R' && startRoutine) {
            break;
        }
        if (startRoutine) {
            // Characters indicate the type of movement
            if (routine_str == 'c') {
                routine.moves[count].type = MOVE_FORWARD_SMART;
                count++;
            } else if (routine_str == 'd') {
                routine.moves[count].type = TURN_TO_ANGLE;
                count++;
            }
            // Numbers indicate the quantity of the movement
            else if (isdigit(routine_str) && !dec) {
                num_buffer[buffer_loc] = routine_str;
                buffer_loc++;
            } else if (routine_str == '.') {
                num_buffer[buffer_loc] = routine_str;
                buffer_loc++;
                dec = 1;
            } else if (dec) {
                num_buffer[buffer_loc] = routine_str;
                buffer_loc++;
                dec_loc++;
            }
            if (dec_loc == 3) {
                routine.moves[count].quantity = atoi(num_buffer);
                dec_loc = 0;
                dec = 0;
                buffer_loc = 0;
                for (i = 0; i < 10; i++) {
                    num_buffer[i] = '\n';
                }
            }
        }
    }
    routine.amount = count;
    // Executes the routine made
    return execute_routine(sensor, &routine, &cybot_pose);
}
// To be done when the edge is found
void send_edge(char edge_type) {
    char buffer[50];
    if (edge_type == 'W' || edge_type == 'E') {
        sprintf(buffer, " E %0.2f %c ", cybot_pose.xy.x, edge_type);
        uart_sendStr(buffer);
    } else {
        sprintf(buffer, " E %0.2f %c ", cybot_pose.xy.y, edge_type);
        uart_sendStr(buffer);
    }
}

// Sets the cybot coordinates
void set_cybot_coords(float x, float y) {
    cybot_pose.xy.x = x;
    cybot_pose.xy.y = y;
}
// Sets the cybot heading
void set_cybot_heading(float heading) {
    cybot_pose.heading = heading;
}

// Finds the next point where the Cybot needs to reach moving towards it's desired location
Coordinate get_interim_coordinate(Coordinate target_coord, Coordinate bot_pos) {
    Coordinate interim_coord;
    int x_dist = target_coord.x - bot_pos.x;
    int y_dist = target_coord.y - bot_pos.y;
    double dist = sqrt((x_dist * x_dist) + (y_dist * y_dist));
    if (dist < 40) {
        return target_coord;
    }
    interim_coord.x = x_dist / (dist / 25);
    interim_coord.y = y_dist / (dist / 25);

    return interim_coord;
}
// Sends the interim coordinates to the App
void send_interim_coordinate(Coordinate interim_coord) {
    char buffer[50];
    sprintf(buffer, " d %0.2f %0.2f %0.2f ", interim_coord.x, interim_coord.y, cybot_pose.heading);
    uart_sendStr(buffer);
}
// Communicates with the GUI the cybot position
void send_bot_pos() {
    char buffer[50];
    sprintf(buffer, " b %0.2f %0.2f %0.2f ", cybot_pose.xy.x, cybot_pose.xy.y, cybot_pose.heading);
    uart_sendStr(buffer);
}
// Communicates with the GUI a hole in the way
void send_hole_point(oi_t* sensor) {
    uint16_t reading = getHoleTouching(sensor);
    float head = cybot_pose.heading;
    if (reading & 0x08) {
        head += 1.178;
    } else if (reading & 0x04) {
        head += 0.393;
    } else if (reading & 0x02) {
        head -= 0.393;
    } else if (reading & 0x01) {
        head -= 1.178;
    }
    char buffer[50];
    sprintf(buffer, " h %0.3f %0.3f %0.3f ", cybot_pose.xy.x, cybot_pose.xy.y, head);
    uart_sendStr(buffer);
}

// Communicates with the GUI objects in the way
void send_field(Field field) {
    send_pillars_through_putty(field.pillars, field.size);
}

// Executes moves in a routine
bumpy execute_move(oi_t* sensor, Move move, Pose2D* robotPose) {
    switch (move.type) {
               case MOVE_FORWARD:
                   move_forward(sensor, move.quantity, robotPose);
                   break;
               case MOVE_BACKWARDS:
                   move_backwards(sensor, move.quantity, robotPose);
                   break;
               case MOVE_FORWARD_SMART:
               {
                   // the only issue with blindly returning here is that we lose the ability to continue through
                   // the routine if we did not hit a pillar, wait nvm we don't
                   return smart_move_forward(sensor, move.quantity, robotPose);
                   // somehow we need to add this to the field
                   // I think that we can blindly return the hit pillar from here, this would mean that we are at the result if the pillar is not effectively null
               }
               case TURN_TO_ANGLE:
                   // lcd_printf("Trying to turn %0.3f", move.quantity);
                   turn_to_angle(sensor, radians_to_degrees(move.quantity), robotPose);
                   break;
               case SCAN:
                   //TODO: Implement
                   break;
           }
    return (bumpy) {.complete = true};
    // return (Pillar) {.radius = -1};
}
// Exectues the movements given in a routine
bumpy execute_routine(oi_t* sensor, Routine* routine, Pose2D* botPose) {
    Move* moves = routine->moves;
    uint8_t head;

    // lcd_printf("quantity: %d", routine->amount);

    for (head = 0; head < routine->amount; head++) {
        // lcd_printf("type: %d", moves[head].type);
       bumpy b = execute_move(sensor, moves[head], botPose);
       if (!b.complete) {
           return b; // return pillar of hit
       }
    } // return robot pose somehow if we want persistent memory of the enviornment
    // wait we def do
    return (bumpy) {.complete = true};
}
