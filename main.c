#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block_matching.h"
#include "ica.h"
#include "utils.h"
#include "video_denoising.h"
#include "warp.h"

// Add these function declarations at the top of the file with the other includes
// Image* load_next_frame(void);  // Declare return type as Image*
// int add_frame_to_buffer(FrameBuffer* buffer, Image* frame);  // Match return type with warp.h

void print_usage(const char* program_name) {
    printf("Usage: %s <reference_image> <target_image> <output_flow_image> [options]\n", program_name);
    printf("\nOptions:\n");
    printf("  -v, --verbose LEVEL     Set verbosity level (0-3, default: 1)\n");
    printf("  -i, --iterations N      Set number of ICA iterations (default: 3)\n");
    printf("  -t, --tile-size N       Set tile size (default: 16)\n");
    printf("  -b, --blur SIGMA        Set Gaussian blur sigma (default: 0.0)\n");
    printf("\nExample:\n");
    printf("  %s ref.png target.png flow.png -v 2 -i 5\n", program_name);
}

// Function to visualize optical flow as an image
Image* visualize_flow(const AlignmentMap* flow) {
    Image* vis = create_image(flow->height * flow->width, 2, 1);
    if (!vis) return NULL;

    // Flatten flow vectors into visualization
    for (int y = 0; y < flow->height; y++) {
        for (int x = 0; x < flow->width; x++) {
            int idx = y * flow->width + x;
            vis->data[idx * 2] = (flow->data[idx].x + 20.0f) / 40.0f;  // Normalize to [0,1]
            vis->data[idx * 2 + 1] = (flow->data[idx].y + 20.0f) / 40.0f;
        }
    }
    return vis;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <input_pattern> <output_pattern> <num_frames> [options]\n", argv[0]);
        printf("Example: %s frame_%%04d.png denoised_%%04d.png 100\n", argv[0]);
        return 1;
    }

    const char* input_pattern = argv[1];
    const char* output_pattern = argv[2];
    int num_frames = atoi(argv[3]);

    // Initialize denoising parameters
    DenoisingParams denoise_params = {
        .temporal_radius = 2,    // Use 5 frames total
        .noise_level = 20.0f,    // Adjust based on your video
        .block_size = 16,
        .search_radius = 16
    };
    
    // Create frame buffer
    FrameBuffer* buffer = create_frame_buffer(2 * denoise_params.temporal_radius + 1);
    if (!buffer) {
        fprintf(stderr, "Failed to create frame buffer\n");
        return 1;
    }

    // Process frames
    for (int frame_idx = 0; frame_idx < num_frames; frame_idx++) {
        Image* frame = load_next_frame(input_pattern, frame_idx);
        if (!frame) {
            fprintf(stderr, "Failed to load frame %d\n", frame_idx);
            continue;
        }

        if (add_frame_to_buffer(buffer, frame) != 0) {
            fprintf(stderr, "Failed to add frame %d to buffer\n", frame_idx);
            free_image(frame);
            continue;
        }

        // Once we have enough frames in the buffer, denoise the center frame
        if (buffer->size == buffer->capacity) {
            Image* denoised = denoise_frame(buffer, &denoise_params);
            if (denoised) {
                char output_filename[256];
                snprintf(output_filename, sizeof(output_filename), output_pattern, 
                        frame_idx - denoise_params.temporal_radius);
                if (!save_image(output_filename, denoised)) {
                    fprintf(stderr, "Failed to save denoised frame %d\n", 
                            frame_idx - denoise_params.temporal_radius);
                }
                free_image(denoised);
            }
        }
        
        free_image(frame);
    }

    // Process remaining frames in buffer
    while (buffer->size > 0) {
        Image* denoised = denoise_frame(buffer, &denoise_params);
        if (denoised) {
            char output_filename[256];
            snprintf(output_filename, sizeof(output_filename), output_pattern, 
                    num_frames - buffer->size);
            if (!save_image(output_filename, denoised)) {
                fprintf(stderr, "Failed to save final denoised frame %d\n", 
                        num_frames - buffer->size);
            }
            free_image(denoised);
        }
    }

    // Cleanup
    free_frame_buffer(buffer);
    printf("Video denoising completed!\n");
    return 0;
}
