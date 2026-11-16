#include <racing/actors.h>
#include <libultra/gbi.h>
#include <main.h>
#include <assets/textures/common_data.h>

/**
 * @brief Renders the green shell actor.
 *
 * @param camera
 * @param matrix
 * @param shell
 */
void render_actor_green_shell(Camera* camera, Mat4 matrix, struct ShellActor* shell) {
    render_actor_shell(camera, matrix, shell);
}
