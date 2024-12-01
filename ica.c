#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "ica.h"

// Helper function declarations
static void gaussian_blur_1d(const float* input, float* output, int size, float sigma);
static void compute_prewitt_gradients(const Image* img, ImageGradients* grads);
static void compute_gaussian_kernel(float* kernel, int size, float sigma);
static void bilinear_interpolation(const Image* img, float x, float y, float* result);

// Implementation of core functions
ImageGradients* init_ica(const Image* ref_img, const ICAParams* params) {
    ImageGradients* grads = (ImageGradients*)malloc(sizeof(ImageGradients));
    if (!grads) return NULL;

    grads->height = ref_img->height;
    grads->width = ref_img->width;
    grads->data_x = (pixel_t*)malloc(sizeof(pixel_t) * grads->height * grads->width);
    grads->data_y = (pixel_t*)malloc(sizeof(pixel_t) * grads->height * grads->width);

    if (!grads->data_x || !grads->data_y) {
        free_image_gradients(grads);
        return NULL;
    }

    // Compute gradients
    compute_image_gradients(ref_img, grads, params->sigma_blur);
    return grads;
}

void compute_image_gradients(const Image* img, ImageGradients* grads, float sigma_blur) {
    // If blur is needed, create a temporary blurred image
    Image* blurred_img = NULL;
    const Image* working_img = img;

    if (sigma_blur > 0) {
        blurred_img = create_image(img->height, img->width, img->channels);
        if (!blurred_img) return;

        // Apply Gaussian blur
        float* temp = (float*)malloc(sizeof(float) * img->width);
        if (!temp) {
            free_image(blurred_img);
            return;
        }

        // Horizontal blur
        for (int y = 0; y < img->height; y++) {
            gaussian_blur_1d(&img->data[y * img->width], &blurred_img->data[y * img->width], 
                           img->width, sigma_blur);
        }

        // Vertical blur
        for (int x = 0; x < img->width; x++) {
            // Extract column
            for (int y = 0; y < img->height; y++) {
                temp[y] = blurred_img->data[y * img->width + x];
            }
            // Blur column
            gaussian_blur_1d(temp, temp, img->height, sigma_blur);
            // Put back column
            for (int y = 0; y < img->height; y++) {
                blurred_img->data[y * img->width + x] = temp[y];
            }
        }

        free(temp);
        working_img = blurred_img;
    }

    // Compute Prewitt gradients
    compute_prewitt_gradients(working_img, grads);

    if (blurred_img) {
        free_image(blurred_img);
    }
}

static void compute_prewitt_gradients(const Image* img, ImageGradients* grads) {
    // Prewitt kernels
    const float kernelx[3] = {-1, 0, 1};
    const float kernely[3] = {-1, 0, 1};

    // Compute horizontal gradients
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            float sum = 0;
            for (int k = -1; k <= 1; k++) {
                int xk = x + k;
                if (xk >= 0 && xk < img->width) {
                    sum += img->data[y * img->width + xk] * kernelx[k + 1];
                }
            }
            grads->data_x[y * img->width + x] = sum;
        }
    }

    // Compute vertical gradients
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            float sum = 0;
            for (int k = -1; k <= 1; k++) {
                int yk = y + k;
                if (yk >= 0 && yk < img->height) {
                    sum += img->data[yk * img->width + x] * kernely[k + 1];
                }
            }
            grads->data_y[y * img->width + x] = sum;
        }
    }
}

HessianMatrix* compute_hessian(const ImageGradients* grads, int tile_size) {
    int n_patches_y = (grads->height + tile_size - 1) / tile_size;
    int n_patches_x = (grads->width + tile_size - 1) / tile_size;

    HessianMatrix* hessian = (HessianMatrix*)malloc(sizeof(HessianMatrix));
    if (!hessian) return NULL;

    hessian->height = n_patches_y;
    hessian->width = n_patches_x;
    hessian->data = (float*)calloc(n_patches_y * n_patches_x * 4, sizeof(float));
    if (!hessian->data) {
        free(hessian);
        return NULL;
    }

    // Compute Hessian for each patch
    for (int py = 0; py < n_patches_y; py++) {
        for (int px = 0; px < n_patches_x; px++) {
            float h00 = 0, h01 = 0, h11 = 0;
            int patch_start_y = py * tile_size;
            int patch_start_x = px * tile_size;

            // Accumulate gradients over patch
            for (int y = 0; y < tile_size; y++) {
                int img_y = patch_start_y + y;
                if (img_y >= grads->height) break;

                for (int x = 0; x < tile_size; x++) {
                    int img_x = patch_start_x + x;
                    if (img_x >= grads->width) break;

                    int idx = img_y * grads->width + img_x;
                    float gx = grads->data_x[idx];
                    float gy = grads->data_y[idx];

                    h00 += gx * gx;
                    h01 += gx * gy;
                    h11 += gy * gy;
                }
            }

            // Store in Hessian matrix (2x2 symmetric matrix stored in row-major order)
            int hidx = (py * n_patches_x + px) * 4;
            hessian->data[hidx + 0] = h00;  // H[0,0]
            hessian->data[hidx + 1] = h01;  // H[0,1]
            hessian->data[hidx + 2] = h01;  // H[1,0]
            hessian->data[hidx + 3] = h11;  // H[1,1]
        }
    }

    return hessian;
}

AlignmentMap* refine_alignment_ica(const Image* ref_img, const Image* alt_img,
                                const ImageGradients* grads,
                                const HessianMatrix* hessian,
                                const AlignmentMap* initial_alignment,
                                const ICAParams* params) {
    // Create a copy of initial alignment to refine
    AlignmentMap* current_alignment = create_alignment_map(initial_alignment->height, initial_alignment->width);
    if (!current_alignment) return NULL;
    memcpy(current_alignment->data, initial_alignment->data, 
           sizeof(Alignment) * initial_alignment->height * initial_alignment->width);

    // Iterate to refine alignment
    for (int iter = 0; iter < params->num_iterations; iter++) {
        // For each patch
        for (int py = 0; py < current_alignment->height; py++) {
            for (int px = 0; px < current_alignment->width; px++) {
                float b[2] = {0, 0};  // Right-hand side of the system
                int patch_start_y = py * params->tile_size;
                int patch_start_x = px * params->tile_size;
                int hidx = (py * hessian->width + px) * 4;

                // Skip if Hessian is singular
                float det = hessian->data[hidx] * hessian->data[hidx + 3] - 
                          hessian->data[hidx + 1] * hessian->data[hidx + 2];
                if (fabs(det) < 1e-10) continue;

                // Current alignment for this patch
                Alignment* curr_align = &current_alignment->data[py * current_alignment->width + px];

                // Accumulate gradient differences over patch
                for (int y = 0; y < params->tile_size; y++) {
                    int ref_y = patch_start_y + y;
                    if (ref_y >= ref_img->height) break;

                    for (int x = 0; x < params->tile_size; x++) {
                        int ref_x = patch_start_x + x;
                        if (ref_x >= ref_img->width) break;

                        // Compute warped position
                        float warped_x = ref_x + curr_align->x;
                        float warped_y = ref_y + curr_align->y;

                        // Skip if outside image bounds
                        if (warped_x < 0 || warped_x >= alt_img->width - 1 ||
                            warped_y < 0 || warped_y >= alt_img->height - 1) continue;

                        // Get interpolated value from alternate image
                        float warped_val;
                        bilinear_interpolation(alt_img, warped_x, warped_y, &warped_val);

                        // Compute temporal gradient
                        float ref_val = ref_img->data[ref_y * ref_img->width + ref_x];
                        float dt = warped_val - ref_val;

                        // Update b vector
                        int grad_idx = ref_y * grads->width + ref_x;
                        b[0] += -grads->data_x[grad_idx] * dt;
                        b[1] += -grads->data_y[grad_idx] * dt;
                    }
                }

                // Solve 2x2 system
                float delta[2];
                solve_2x2_system(&hessian->data[hidx], b, delta);

                // Update alignment
                curr_align->x += delta[0];
                curr_align->y += delta[1];
            }
        }
    }

    return current_alignment;
}

void solve_2x2_system(const float* A, const float* b, float* x) {
    float det = A[0] * A[3] - A[1] * A[2];
    if (fabs(det) < 1e-10) {
        x[0] = x[1] = 0;
        return;
    }

    float inv_det = 1.0f / det;
    x[0] = (A[3] * b[0] - A[1] * b[1]) * inv_det;
    x[1] = (-A[2] * b[0] + A[0] * b[1]) * inv_det;
}

static void gaussian_blur_1d(const float* input, float* output, int size, float sigma) {
    int radius = (int)(4 * sigma + 0.5);
    float* kernel = (float*)malloc(sizeof(float) * (2 * radius + 1));
    if (!kernel) return;

    compute_gaussian_kernel(kernel, 2 * radius + 1, sigma);

    // Apply convolution
    for (int i = 0; i < size; i++) {
        float sum = 0;
        float weight_sum = 0;

        for (int k = -radius; k <= radius; k++) {
            int idx = i + k;
            if (idx >= 0 && idx < size) {
                float weight = kernel[k + radius];
                sum += input[idx] * weight;
                weight_sum += weight;
            }
        }

        output[i] = sum / weight_sum;
    }

    free(kernel);
}

static void compute_gaussian_kernel(float* kernel, int size, float sigma) {
    int radius = size / 2;
    float sum = 0;

    for (int i = 0; i < size; i++) {
        int x = i - radius;
        kernel[i] = expf(-(x * x) / (2 * sigma * sigma));
        sum += kernel[i];
    }

    // Normalize
    for (int i = 0; i < size; i++) {
        kernel[i] /= sum;
    }
}

static void bilinear_interpolation(const Image* img, float x, float y, float* result) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float dx = x - x0;
    float dy = y - y0;

    float v00 = img->data[y0 * img->width + x0];
    float v10 = img->data[y0 * img->width + x1];
    float v01 = img->data[y1 * img->width + x0];
    float v11 = img->data[y1 * img->width + x1];

    *result = (1 - dx) * (1 - dy) * v00 +
              dx * (1 - dy) * v10 +
              (1 - dx) * dy * v01 +
              dx * dy * v11;
}

void free_image_gradients(ImageGradients* grads) {
    if (grads) {
        free(grads->data_x);
        free(grads->data_y);
        free(grads);
    }
}

void free_hessian_matrix(HessianMatrix* hessian) {
    if (hessian) {
        free(hessian->data);
        free(hessian);
    }
}
