def generate_led_config(num_sections, leds_per_section, speed=1.0, color_offsets=None, cascade_base="PI / 5"):
    """
    Generates JSON for LED mathematical configurations.
    
    :param num_sections: Number of distinct LED groups (rows/sections).
    :param leds_per_section: Number of LEDs in each group.
    :param speed: Multiplier for time 'T' (1.0 is default speed).
    :param color_offsets: Dictionary of RGB phase offsets.
    :param cascade_base: The base string for the section-to-section overlay shift.
    """
    if color_offsets is None:
        color_offsets = {
            "red": "0",
            "green": "2 * PI / 3",
            "blue": "4 * PI / 3"
        }

    leds = []
    
    # Format the time variable with speed
    t_str = "T" if speed == 1.0 else f"(T * {speed})"

    for section in range(num_sections):
        # Calculate how one section overlays the next (the cascade shift)
        if section == 0:
            cascade_str = ""
        else:
            # Multiplies the cascade base by 2, 4, 6, 8 for subsequent rows
            cascade_str = f" - ({section * 2} * {cascade_base})"
        
        base_time = f"{t_str}{cascade_str}"

        # Generate the identical LEDs for this specific section
        for _ in range(leds_per_section):
            led = {}
            for color in ["red", "green", "blue"]:
                offset = color_offsets[color]
                if offset == "0":
                    color_expr = f"(SIN({base_time}) * 127) + 128"
                else:
                    color_expr = f"(SIN({base_time} - ({offset})) * 127) + 128"
                led[color] = color_expr
            
            leds.append(led)
            
    # Format exactly like the requested single-line JSON structures
    output = '{\n  "leds": [\n'
    for i, led in enumerate(leds):
        comma = "," if i < len(leds) - 1 else ""
        output += f'      {{ "red": "{led["red"]}", "green": "{led["green"]}", "blue": "{led["blue"]}" }}{comma}\n'
    output += '    ]\n}'
    
    return output

# --- EXECUTION ---

# Parameters to match the exact output from your prompt:
# - 5 Sections
# - 14 LEDs per section
# - Standard T (speed = 1.0)
# - Standard RGB phase shifts
# - Cascade overlay shifting by (2 * PI / 5) per section

json_output = generate_led_config(
    num_sections=5,
    leds_per_section=14,
    speed=1.0, 
    color_offsets={
        "red": "0",
        "green": "2 * PI / 3",
        "blue": "4 * PI / 3"
    },
    cascade_base="PI / 5"
)

print(json_output)
