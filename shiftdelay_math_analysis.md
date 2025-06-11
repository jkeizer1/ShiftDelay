# ShiftDelay: Mathematical DSP Analysis

## Core Mathematical Framework

### Primary Phase-Frequency Transform

The fundamental innovation lies in the direct time-domain phase shifting function:

```
τ_phase(φ, f) = (φ/360°) × (1/f) = φ/(360° × f)
```

Where:
- `τ_phase` = phase-induced delay time (seconds)
- `φ` = phase offset (degrees) 
- `f` = fundamental frequency (Hz)

This creates a **frequency-dependent delay** that maintains constant phase relationships across the frequency spectrum, contrasting with traditional all-pass filter phasers.

### Composite Delay Function

The total delay combines frequency-adaptive and frequency-independent components:

```
τ_total(t) = τ_phase(φ(t), f(t)) + τ_base(t)
```

Where:
- `τ_base(t)` = time-invariant delay component [0.5, 6.0] seconds
- `f(t) = 440 × 2^(CV(t))` = CV-to-frequency conversion (1V/octave)

### Exponential Parameter Smoothing

All time-varying parameters employ first-order exponential smoothing:

```
y[n] = α × y[n-1] + (1-α) × x[n]
```

Where smoothing coefficient α is derived from time constants:

```
α = e^(-1/(T_smooth × f_s))
```

**Adaptive Smoothing Rates:**
- Base smoothing: `T_base = 2ms` (fast response)
- Pot smoothing: `T_pot = 50ms` (slow, stable)
- Wet/dry mix: `T_base = 2ms`

### Stability-Dependent Smoothing Logic

```matlab
α(t) = {
  α_base,     if |τ_base(t) - τ_base(t-Δt)| > ε
  α_pot,      if stable_time > T_stabilization
}
```

Where `T_stabilization = 20ms` prevents oscillation during parameter adjustment.

## Delay Line Implementation

### Fractional Delay Interpolation

Reading from delay line with sub-sample precision:

```
r = w - τ × f_s
y[n] = (1-frac) × buffer[⌊r⌋] + frac × buffer[⌊r⌋+1]
```

Where:
- `w` = write pointer position
- `frac = r - ⌊r⌋` = fractional component
- Circular buffer indexing: `index = r mod N_buffer`

### Crossfading During Parameter Changes

When delay parameters change significantly, smooth transition via linear crossfading:

```
y[n] = (1-t) × y_old[n] + t × y_new[n]
```

Where `t ∈ [0,1]` over crossfade duration `T_crossfade = 50ms`.

## Frequency Response Analysis

### Traditional All-Pass Phaser

Standard phaser uses cascaded all-pass filters:

```
H_AP(ω) = (1 - g × e^(-jωτ))/(1 - g × e^(jωτ))
```

- Maintains constant magnitude: `|H_AP(ω)| = 1`
- Creates frequency-dependent phase shift: `∠H_AP(ω) = -2 × arctan(g × sin(ωτ)/(1 - g × cos(ωτ)))`

### Time-Domain Phase Shifter (This Algorithm)

Direct delay implementation:

```
H_delay(ω) = e^(-jωτ_phase(φ,f))
```

**Key Differences:**
1. **Magnitude Response**: `|H_delay(ω)| = 1` (like all-pass) BUT with frequency-dependent delay
2. **Phase Response**: `∠H_delay(ω) = -ω × τ_phase(φ,f) = -ω × φ/(360° × f)`
3. **Musical Coherence**: Phase shift scales inversely with frequency, maintaining harmonic relationships

### Phase Relationship Preservation

For harmonic series at fundamental `f₀`:

```
τ_phase(φ, n×f₀) = φ/(360° × n×f₀) = τ_phase(φ, f₀)/n
```

This ensures harmonics maintain proportional phase relationships, unlike fixed all-pass filters.

## Signal Flow Equations

### Complete Processing Chain

```matlab
% Input conditioning
x_scaled[n] = x[n] × 0.1

% CV to frequency conversion  
f[n] = 440 × 2^(CV[n])

% Phase delay calculation
τ_p[n] = φ[n]/(360° × max(f[n], 20))

% Parameter smoothing
τ_base_smooth[n] = α × τ_base_smooth[n-1] + (1-α) × τ_base[n]
mix_smooth[n] = α × mix_smooth[n-1] + (1-α) × mix[n]

% Total delay computation
τ_total[n] = τ_p[n] + τ_base_smooth[n]

% Delay line processing
y_delayed[n] = DelayInterp(x_scaled[n], τ_total[n])

% Wet/dry mixing
y[n] = (mix_smooth[n] × y_delayed[n] + (1-mix_smooth[n]) × x_scaled[n]) × 10
```

## Theoretical Advantages Over All-Pass Phasers

1. **Harmonic Preservation**: Phase relationships scale correctly with frequency
2. **Musical Tuning**: Phase shifts relate directly to musical intervals
3. **Predictable Behavior**: Linear phase-frequency relationship vs. complex all-pass response
4. **Extended Range**: Can achieve phase shifts impossible with practical all-pass networks
5. **CV Integration**: Natural integration with pitch CV for musical applications

## Computational Complexity

- **Per-sample operations**: O(1) - constant time interpolated delay read
- **Memory**: O(f_s × τ_max) - linear in sample rate and maximum delay
- **Stability**: Unconditionally stable (pure delay, no feedback)

## Limitations and Considerations

1. **Group Delay**: Introduces actual time delay (unlike all-pass filters)
2. **Memory Usage**: Requires large delay buffer for long delay times
3. **Frequency Limits**: Low-frequency performance limited by maximum delay buffer size
4. **Aliasing**: No anti-aliasing for extreme parameter modulation (could add pre-filtering)

This mathematical framework demonstrates how time-domain phase shifting can achieve musical phase relationships impossible with traditional frequency-domain approaches, while maintaining the computational efficiency required for real-time embedded processing.