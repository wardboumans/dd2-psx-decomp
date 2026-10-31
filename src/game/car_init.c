#include "game/car.h"
#include "game/car_cluts.h"
#include "game/car_draw.h"
#include "game/car_init.h"
#include "game/car_motion.h"
#include "game/frontend.h"
#include "game/main_init.h"
#include "game/play_game.h"
#include "game/scene.h"
#include "game/wheels.h"

#include <string.h>

Object car_objects[MAX_CARS][2];
SVECTOR car_vertices[MAX_CARS][CAR_MODEL_VERTICES];
MATRIX car_colour_matrix[MAX_CARS];
MATRIX car_light_matrix;

Object wild_bill_objects[2];          /* 0x800848DC, 0x8008C464 */
RollerCar roller_cars[ROLLER_CARS];   /* 0x80088608 */
Object roller_objects[ROLLER_CARS];   /* 0x8009EA3C */

/* The car models with their paint, doors, wheels, shadows and brake lights, lit by the level's light. */
void Init_Car_Graphics(void)
{
    int player = current_player_car;
    Init_Car_Cluts();
    Create_Object(&car_objects[player][0], (Mesh *)level_data_section(SECTION_PLAYER_CAR));
    Create_Object(&car_objects[player][1], (Mesh *)level_data_section(SECTION_CAR_NEAR));
    Apply_Car_Part_Cluts(player, player, 0);
    const int16_t *colour = light_colours[current_level - 1];
    for (int i = 0; i < num_cars; i++) {
        MATRIX *m = &car_colour_matrix[i];
        memset(m->m, 0, sizeof(m->m));
        m->m[0][0] = colour[0];
        m->m[1][0] = colour[1];
        m->m[2][0] = colour[2];
        if (i != player) {
            Create_Object(&car_objects[i][0], (Mesh *)level_data_section(SECTION_CAR_FAR));
            Create_Object(&car_objects[i][1], (Mesh *)level_data_section(SECTION_CAR_NEAR));
            Apply_Car_Part_Cluts(i, i, 1);
            Apply_Car_Part_Cluts(i, i, 2);
        }
    }
    const Mesh *near_model = (const Mesh *)level_data_section(SECTION_CAR_NEAR);
    for (int i = 0; i < num_cars; i++)
        if (i != player)
            memcpy(car_vertices[i], mesh_section(near_model, near_model->vertices), sizeof(car_vertices[i]));
    Init_Car_Doors();
    for (int i = 0; i < num_cars; i++) {
        Set_Car_Door_Texture(i, i, 0, 0, 0);
        Set_Car_Door_Texture(i, i, 0, 1, 0);
        Set_Car_Door_Texture(i, i, 1, 0, 0);
        Set_Car_Door_Texture(i, i, 1, 1, 0);
    }
    Init_Wheel_Objects();
    Init_Car_Shadows();
    const int16_t *light = light_directions[current_level - 1];
    car_light_matrix.m[0][0] = light[0];
    car_light_matrix.m[0][1] = light[1];
    car_light_matrix.m[0][2] = light[2];
    Init_Brake_Lights();
}

void Init_Wild_Bill(void)
{
    Create_Object(&wild_bill_objects[0], (Mesh *)level_data_section(21));
    Create_Object(&wild_bill_objects[1], (Mesh *)level_data_section(22));
}

/* The rollercoaster's train: an engine and eleven cars, waiting at the start of the ride. */
void Init_Rollercoaster(void)
{
    static const int32_t start[2][4] = {{-64368, 670, 19967, 0}, {-62894, 1487, 18607, 0}}; /* 0x8006EC88 */
    for (int i = 0; i < ROLLER_CARS; i++) {
        RollerCar *r = &roller_cars[i];
        memset(r->position, 0, sizeof(r->position));
        r->point_index = r->step = r->t = 0;
        memcpy(r->point[0], start[0], sizeof(start[0]));
        memcpy(r->point[1], start[1], sizeof(start[1]));
        r->delay = i * 2;
        r->ready = 0;
        r->angle[0] = 0;
        r->angle[1] = 0xdda;
        r->angle[2] = 0;
        r->restart_point = -1;
        if (i == 0) {
            Create_Object(&roller_objects[0], (Mesh *)level_data_section(21));
            roller_cars[0].ready = 0;
            roller_cars[0].restart_point = -1;
        } else {
            Create_Object(&roller_objects[i], (Mesh *)level_data_section(22));
        }
    }
}
