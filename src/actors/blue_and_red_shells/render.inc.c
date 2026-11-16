#include <racing/actors.h>
#include <libultra/gbi.h>

/**
 * @brief Render the red shell actor
 *
 * @param camera
 * @param matrix
 * @param shell
 */
void render_actor_red_shell(Camera* camera, Mat4 matrix, struct ShellActor* shell) {
    // Set up color combiner for channel swapping
    render_actor_shell(camera, matrix, shell);
}

/**
 * @brief Render the blue shell actor
 *
 * @param camera
 * @param matrix
 * @param shell
 */
void render_actor_blue_shell(Camera* camera, Mat4 matrix, struct ShellActor* shell) {
    render_actor_shell(camera, matrix, shell);
}
