#set page(width: 8.5in, height: 11in, margin: 1in)
#set text(font: "New Computer Modern", size: 13pt)
#set heading(numbering: "1.")
#set par(justify: true)

= Introduction

We are going to be solving and implementing the *Time dependent Schrödinger equation*, which tells us how quantum particles behave.

*Goal:* We want to simulate and visualize a quantum particle (like an electron) moving in 3D space over time.

== The main equation 


$ i planck (partial psi)/(partial t) = hat(H) psi $

*Meaning symbols*
- *$psi$ (psi)* = the "wavefunction" — describes where the particle might be
- *$t$* = time
- *$planck$ (h-bar)* = Planck's constant ÷ $2pi$ (a fundamental constant in quantum mechanics)
- *$hat(H)$ (H-hat)* = Hamiltonian (the total energy operator)
- *$i$* = the imaginary number $sqrt(-1)$

The Hamiltonian has two parts:
$ hat(H) = -(planck^2)/(2m) nabla^2 + V(bold(r), t) $
*Meaning Parts*
- *$-(planck^2)/(2m) nabla^2$* = kinetic energy (energy from motion)
  - *$m$* = mass of the particle
  - *$nabla^2$* = Laplacian operator (measures how the wavefunction curves in space)
- *$V(bold(r), t)$* = potential energy (energy from forces, like electric fields)

*Probability:* The quantity $|psi|^2$ tells you the probability of finding the particle at each location.

=== Simplified Units

To make the math easier, we will for now set $planck = 1$ and $m = 1$. This gives us:
$ i (partial psi)/(partial t) = [-1/2 nabla^2 + V(bold(r), t)] psi $

We can convert back to real units later by putting $planck$ and $m$ back in.

== How to Solve It: The Split-Step Method

We want to move the wavefunction forward in time by small steps $Delta t$.

The exact solution would be:
$ psi(t + Delta t) = exp(-i hat(H) Delta t \/ planck) psi(t) $

But this is hard to compute directly! So we *split* the Hamiltonian into two easier parts:

- *$hat(T)$* = kinetic energy part = $-(planck^2)/(2m) nabla^2$
- *$hat(U)$* = potential energy part = $V(bold(r), t)$

The *Strang splitting* method says we can approximate one time step as:

#align(center)[
  _[half kinetic step] → [full potential step] → [half kinetic step]_
]

Mathematically:
$ exp(-i hat(H) Delta t \/ planck) approx exp(-i hat(T) (Delta t)/(2planck)) dot.c exp(-i hat(U) (Delta t)/(planck)) dot.c exp(-i hat(T) (Delta t)/(2planck)) $

This is accurate to order $Delta t^2$ (very good for small time steps).

== Why Use FFTs? (The Clever Trick)

*The Problem:* The kinetic and potential energy parts are hard to compute in the same space.

*The Solution:* Use different spaces for each part!

=== Potential Energy (Easy in Real Space)

In regular 3D space (where $x, y, z$ are positions), applying the potential energy is simple multiplication:
$ psi_"new"(bold(r)) = exp(-i V(bold(r), t) Delta t \/ planck) dot.c psi_"old"(bold(r)) $

You just multiply each point by a phase factor.

=== Kinetic Energy (Easy in Momentum Space)

In *Fourier space* (also called k-space or momentum space), the kinetic energy is also simple multiplication:
$ tilde(psi)_"new"(bold(k)) = exp(-i (planck |bold(k)|^2)/(2m) Delta t) dot.c tilde(psi)_"old"(bold(k)) $

Where *$bold(k)$* is the wave vector (related to momentum by $bold(p) = planck bold(k)$).

=== The Bridge: Fast Fourier Transform (FFT)

- *FFT* converts from real space $(x, y, z)$ to momentum space $(k_x, k_y, k_z)$
- *IFFT* (inverse FFT) converts back

So the algorithm alternates:
+ Real space → FFT → momentum space → apply kinetic energy
+ IFFT → real space → apply potential energy
+ Repeat

== Setting Up the Grid

We simulate on a 3D box with dimensions $L_x times L_y times L_z$, divided into $N_x times N_y times N_z$ grid points.

=== Real Space Grid

- Spacing: $Delta x = L_x \/ N_x$, $Delta y = L_y \/ N_y$, $Delta z = L_z \/ N_z$
- Points: $x_i = -L_x\/2 + i Delta x$, for $i = 0, 1, dots, N_x - 1$
- (Same for $y$ and $z$)

=== Momentum Space Grid

The FFT gives you momentum values:
$ k_x(p) = (2pi)/L_x times cases(
  0\, 1\, 2\, dots\, N_x\/2-1\, -N_x\/2\, dots\, -1
) $

(Same pattern for $k_y$ and $k_z$)

The squared magnitude is:
$ k^2 = k_x^2 + k_y^2 + k_z^2 $

*Precompute these grids once at the start!*

== The Complete Algorithm (Step-by-Step)

=== Setup (do once):

+ Compute the k-space grid values $(k_x, k_y, k_z, "and" k^2)$
+ Precompute the kinetic factor: 
  $ K(bold(k)) = exp(-i k^2 Delta t \/ 4) $
  (half-step with $planck = m = 1$)
+ Precompute the potential factor:
  $ P(bold(r)) = exp(-i V(bold(r)) Delta t) $
  (if $V$ doesn't change in time)

=== Each Time Step (repeat many times):

*1. First half kinetic step (in momentum space):*
- Transform: $tilde(psi) <- "FFT"[psi]$
- Multiply: $tilde(psi) <- K(bold(k)) dot.c tilde(psi)$
- Transform back: $psi <- "IFFT"[tilde(psi)]$

*2. Full potential step (in real space):*
- Multiply: $psi <- P(bold(r)) dot.c psi$

*3. Second half kinetic step (in momentum space):*
- Transform: $tilde(psi) <- "FFT"[psi]$
- Multiply: $tilde(psi) <- K(bold(k)) dot.c tilde(psi)$
- Transform back: $psi <- "IFFT"[tilde(psi)]$

=== Visualization:

After each time step (or every few steps), compute the probability density:
$ rho(bold(r)) = |psi(bold(r))|^2 $

This is what you display as a 3D volume or isosurface!

== Key Points to Remember

- *$psi$ is complex:* It has real and imaginary parts (or magnitude and phase)
- *$|psi|^2$ is real:* This is what you see — the probability density
- *FFT is the speed trick:* It makes the kinetic energy easy to compute
- *Small time steps:* Smaller $Delta t$ gives more accurate results
- *Normalization:* The total probability should stay 1:
  $ integral |psi|^2 dif^3 r = 1 $

== Summary

The split-step FFT method works by:

+ Splitting the problem into kinetic and potential parts
+ Using FFTs to switch between real space (good for potential) and momentum space (good for kinetic energy)
+ Taking small time steps, alternating between the two spaces
+ Visualizing $|psi|^2$ to see where the particle is likely to be

This method is fast, accurate, and perfect for GPU implementation!

= Implementation

== On Setup 
- Compute k-space grid values $(k_x, k_y, k_z, "and" k^2)$
- Compute kinetic factor: 
  $ K(bold(k)) = exp(-i k^2 Delta t \/ 4) $
  (half-step with $planck = m = 1$)
- Compute the potential factor:
  $ P(bold(r)) = exp(-i V(bold(r)) Delta t) $
  (if $V$ doesn't change in time)
- Initialize guassian wavepacket (can use something else if wanted):
$ psi(bold(r), t=0) = A exp(-((x-x_0)^2 + (y-y_0)^2 + (z-z_0)^2)/(4sigma^2) + i bold(k)_0 dot.c bold(r)) $

*Meaning*
1. *Position part* (real Gaussian):
  $ exp(-r^2/(4sigma^2)) $
  - Centers the particle at position $(x_0, y_0, z_0)$
  - $sigma$ = width parameter (how spread out it is)

2. *Momentum part* (plane wave):
  $ exp(i bold(k)_0 dot.c bold(r)) = exp(i(k_(0x)x + k_(0y)y + k_(0z)z)) $
  - Gives the particle initial momentum
  - $bold(k)_0 = (k_(0x), k_(0y), k_(0z))$ = initial wave vector

3. *Normalization constant*:
  $ A = (pi sigma^2)^(-3\/4) $
== On Update 

*First half kinetic step (in Fourier space):*
- Transform: $tilde(psi) <- "FFT"[psi]$
- Multiply: $tilde(psi) <- K(bold(k)) dot.c tilde(psi)$
- Transform back: $psi <- "IFFT"[tilde(psi)]$

*Full potential step (in real space):*
- Multiply: $psi <- P(bold(r)) dot.c psi$

*Second half kinetic step (in Fourier space):*
- Transform: $tilde(psi) <- "FFT"[psi]$
- Multiply: $tilde(psi) <- K(bold(k)) dot.c tilde(psi)$
- Transform back: $psi <- "IFFT"[tilde(psi)]$

*Visualizing*
- After $N$ steps we compute the probability density in 3D volume
$ rho(bold(r)) = |psi(bold(r))|^2$

