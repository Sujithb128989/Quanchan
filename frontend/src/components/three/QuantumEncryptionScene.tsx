import { useRef, useMemo } from 'react';
import { Canvas, useFrame, useLoader } from '@react-three/fiber';
import { EffectComposer, Bloom, Vignette, ChromaticAberration } from '@react-three/postprocessing';
import { BlendFunction } from 'postprocessing';
import * as THREE from 'three';
import { TextureLoader } from 'three';

// ─── Bloch Sphere (qubit) ────────────────────────────────────────────────────
function BlochSphere({ index, phase }: { index: number; phase: number }) {
    const groupRef = useRef<THREE.Group>(null);
    const sphereRef = useRef<THREE.Mesh>(null);
    const ringRef = useRef<THREE.Mesh>(null);

    const orbitRadius = 4.5;
    const baseAngle = (index / 12) * Math.PI * 2;

    useFrame((state) => {
        if (!groupRef.current || !sphereRef.current || !ringRef.current) return;
        const t = state.clock.elapsedTime;
        const angle = baseAngle + t * 0.4;
        const y = Math.sin(baseAngle * 2 + t * 0.6) * 1.2;
        groupRef.current.position.set(
            Math.cos(angle) * orbitRadius,
            y,
            Math.sin(angle) * orbitRadius
        );
        sphereRef.current.rotation.y += 0.03;
        sphereRef.current.rotation.x += 0.015;
        ringRef.current.rotation.z += 0.02;

        // Color shift: red → cyan based on phase
        const mat = sphereRef.current.material as THREE.MeshStandardMaterial;
        const r = 1 - phase;
        const g = 0.27 + phase * 0.73;
        const b = phase;
        mat.color.setRGB(r, g, b);
        mat.emissive.setRGB(r * 0.5, g * 0.3, b * 0.5);

        // Ring color
        const ringMat = ringRef.current.material as THREE.MeshBasicMaterial;
        ringMat.color.setRGB(r, g, b);
    });

    return (
        <group ref={groupRef}>
            <mesh ref={sphereRef}>
                <sphereGeometry args={[0.28, 24, 24]} />
                <meshStandardMaterial
                    color="#ffd700"
                    metalness={0.9}
                    roughness={0.1}
                    emissive="#ff2200"
                    emissiveIntensity={0.6}
                />
            </mesh>
            <mesh ref={ringRef} rotation={[Math.PI / 2, 0, 0]}>
                <torusGeometry args={[0.45, 0.025, 8, 32]} />
                <meshBasicMaterial color="#ffd700" transparent opacity={0.8} />
            </mesh>
        </group>
    );
}

// ─── Entanglement Lines ───────────────────────────────────────────────────────
function EntanglementLines({ phase }: { phase: number }) {
    const ref = useRef<THREE.Line>(null);
    const positions = useMemo(() => new Float32Array(12 * 2 * 3), []);

    useFrame((state) => {
        if (!ref.current) return;
        const t = state.clock.elapsedTime;
        const orbitRadius = 4.5;
        const pairs = [[0, 6], [1, 7], [2, 8], [3, 9], [4, 10], [5, 11]];
        let idx = 0;
        for (const [a, b] of pairs) {
            const aAngle = (a / 12) * Math.PI * 2 + t * 0.4;
            const bAngle = (b / 12) * Math.PI * 2 + t * 0.4;
            const ay = Math.sin((a / 12) * Math.PI * 4 + t * 0.6) * 1.2;
            const by = Math.sin((b / 12) * Math.PI * 4 + t * 0.6) * 1.2;
            positions[idx++] = Math.cos(aAngle) * orbitRadius;
            positions[idx++] = ay;
            positions[idx++] = Math.sin(aAngle) * orbitRadius;
            positions[idx++] = Math.cos(bAngle) * orbitRadius;
            positions[idx++] = by;
            positions[idx++] = Math.sin(bAngle) * orbitRadius;
        }
        (ref.current.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;

        const mat = ref.current.material as THREE.LineBasicMaterial;
        const r = 1 - phase;
        const g = phase;
        const b = phase;
        mat.color.setRGB(r, g, b);
        mat.opacity = 0.4 + Math.sin(state.clock.elapsedTime * 2) * 0.3;
    });

    return (
        <>
            {/* @ts-ignore */}
            <line ref={ref as any}>
                <bufferGeometry>
                    {/* @ts-ignore */}
                    <bufferAttribute attach="attributes-position" count={24} array={positions} itemSize={3} />
                </bufferGeometry>
                <lineBasicMaterial color="#ffd700" transparent opacity={0.7} linewidth={2} />
            </line>
        </>
    );
}

// ─── Quantum Gate Symbol ──────────────────────────────────────────────────────
function QuantumGate({ phase }: { gateType: string; phase: number }) {
    const ref = useRef<THREE.Group>(null);
    useFrame((state) => {
        if (!ref.current) return;
        const t = state.clock.elapsedTime;
        ref.current.rotation.y = t * 0.8;
        ref.current.rotation.x = Math.sin(t * 0.5) * 0.3;
        const scale = Math.max(0, Math.sin(phase * Math.PI));
        ref.current.scale.setScalar(scale * 0.8);
        ref.current.position.y = Math.sin(t * 1.2) * 0.5;
    });

    const color = new THREE.Color(0, 0.94, 1); // cyan

    return (
        <group ref={ref}>
            <mesh>
                <boxGeometry args={[0.8, 0.8, 0.1]} />
                <meshBasicMaterial color={color} transparent opacity={0.15} wireframe />
            </mesh>
            <mesh>
                <boxGeometry args={[0.85, 0.85, 0.12]} />
                <meshBasicMaterial color={color} transparent opacity={0.5} wireframe />
            </mesh>
        </group>
    );
}

// ─── Phoenix Hologram ─────────────────────────────────────────────────────────
function PhoenixCenter({ texture, phase }: { texture: THREE.Texture; phase: number }) {
    const ref = useRef<THREE.Mesh>(null);
    useFrame((state) => {
        if (!ref.current) return;
        const t = state.clock.elapsedTime;
        ref.current.rotation.y = Math.sin(t * 0.5) * 0.4;
        const scalePulse = 1 + Math.sin(t * 2) * 0.08;
        ref.current.scale.setScalar(scalePulse);
        (ref.current.material as THREE.MeshBasicMaterial).opacity =
            0.6 + Math.sin(t * 1.5) * 0.2 + phase * 0.2;
    });

    return (
        <mesh ref={ref}>
            <planeGeometry args={[3, 3]} />
            <meshBasicMaterial
                map={texture}
                transparent
                opacity={0.8}
                side={THREE.DoubleSide}
                depthWrite={false}
            />
        </mesh>
    );
}

// ─── God Ray Lines ────────────────────────────────────────────────────────────
function GodRays() {
    const ref = useRef<THREE.Group>(null);
    useFrame((state) => {
        if (ref.current) {
            ref.current.rotation.z = state.clock.elapsedTime * 0.3;
        }
    });

    return (
        <group ref={ref}>
            {Array.from({ length: 8 }).map((_, i) => {
                const angle = (i / 8) * Math.PI * 2;
                return (
                    <mesh key={i} rotation={[0, 0, angle]}>
                        <planeGeometry args={[0.04, 8]} />
                        <meshBasicMaterial
                            color="#ffd700"
                            transparent
                            opacity={0.06 + i * 0.01}
                            depthWrite={false}
                            blending={THREE.AdditiveBlending}
                        />
                    </mesh>
                );
            })}
        </group>
    );
}

// ─── Particle Explosion (climax) ──────────────────────────────────────────────
function ParticleExplosion({ triggered }: { triggered: boolean }) {
    const ref = useRef<THREE.Points>(null);
    const COUNT = 5000;

    const { positions, velocities, colors } = useMemo(() => {
        const positions = new Float32Array(COUNT * 3);
        const velocities = new Float32Array(COUNT * 3);
        const colors = new Float32Array(COUNT * 3);
        for (let i = 0; i < COUNT; i++) {
            positions[i * 3] = 0; positions[i * 3 + 1] = 0; positions[i * 3 + 2] = 0;
            const theta = Math.random() * Math.PI * 2;
            const phi = Math.acos(2 * Math.random() - 1);
            const speed = 0.08 + Math.random() * 0.25;
            velocities[i * 3] = speed * Math.sin(phi) * Math.cos(theta);
            velocities[i * 3 + 1] = speed * Math.sin(phi) * Math.sin(theta);
            velocities[i * 3 + 2] = speed * Math.cos(phi);
            const t = Math.random();
            if (t < 0.33) { colors[i * 3] = 1; colors[i * 3 + 1] = 1; colors[i * 3 + 2] = 0; }         // yellow
            else if (t < 0.66) { colors[i * 3] = 0; colors[i * 3 + 1] = 0.94; colors[i * 3 + 2] = 1; } // cyan
            else { colors[i * 3] = 1; colors[i * 3 + 1] = 0.84; colors[i * 3 + 2] = 0; }               // gold
        }
        return { positions, velocities, colors };
    }, []);

    const progressRef = useRef(0);

    useFrame((_, delta) => {
        if (!ref.current || !triggered) return;
        progressRef.current = Math.min(progressRef.current + delta * 1.2, 1);
        const pos = ref.current.geometry.attributes.position.array as Float32Array;
        for (let i = 0; i < COUNT; i++) {
            pos[i * 3] += velocities[i * 3] * delta * 60 * (1 - progressRef.current * 0.7);
            pos[i * 3 + 1] += velocities[i * 3 + 1] * delta * 60 * (1 - progressRef.current * 0.7);
            pos[i * 3 + 2] += velocities[i * 3 + 2] * delta * 60 * (1 - progressRef.current * 0.7);
        }
        ref.current.geometry.attributes.position.needsUpdate = true;
        (ref.current.material as THREE.PointsMaterial).opacity = Math.max(0, 1 - progressRef.current);
    });

    return (
        <points ref={ref} visible={triggered}>
            <bufferGeometry>
                {/* @ts-ignore */}
                <bufferAttribute attach="attributes-position" count={COUNT} array={positions} itemSize={3} />
                {/* @ts-ignore */}
                <bufferAttribute attach="attributes-color" count={COUNT} array={colors} itemSize={3} />
            </bufferGeometry>
            <pointsMaterial
                size={0.06}
                vertexColors
                transparent
                opacity={1}
                sizeAttenuation
                blending={THREE.AdditiveBlending}
                depthWrite={false}
            />
        </points>
    );
}

// ─── Full Scene ───────────────────────────────────────────────────────────────
function QuantumScene({ phase, explode }: { phase: number; explode: boolean }) {
    const texture = useLoader(TextureLoader, '/phoenix.png');
    const gateIndex = Math.floor(phase * 4);
    const gateTypes = ['H', 'CNOT', 'T', 'X'];

    return (
        <>
            <ambientLight intensity={0.2} color="#ffd700" />
            <pointLight color="#ffd700" intensity={3} position={[0, 3, 3]} />
            <pointLight color="#00f0ff" intensity={2} position={[0, -3, -3]} />

            <PhoenixCenter texture={texture} phase={phase} />
            <GodRays />

            {Array.from({ length: 12 }).map((_, i) => (
                <BlochSphere key={i} index={i} phase={phase} />
            ))}

            <EntanglementLines phase={phase} />

            {gateIndex < 4 && (
                <group position={[gateIndex % 2 === 0 ? -6 : 6, (gateIndex - 1.5) * 1.5, 0]}>
                    <QuantumGate gateType={gateTypes[gateIndex]} phase={(phase * 4) % 1} />
                </group>
            )}

            <ParticleExplosion triggered={explode} />

            <EffectComposer>
                <Bloom
                    intensity={2.5}
                    radius={0.8}
                    luminanceThreshold={0.1}
                    luminanceSmoothing={0.9}
                    blendFunction={BlendFunction.ADD}
                />
                <ChromaticAberration
                    offset={[0.002, 0.002] as unknown as THREE.Vector2}
                    blendFunction={BlendFunction.NORMAL}
                    radialModulation={false}
                    modulationOffset={0}
                />
                <Vignette eskil={false} offset={0.3} darkness={0.9} />
            </EffectComposer>
        </>
    );
}

interface QuantumEncryptionSceneProps {
    phase: number;      // 0..1
    explode: boolean;
}

export default function QuantumEncryptionScene({ phase, explode }: QuantumEncryptionSceneProps) {
    return (
        <Canvas
            camera={{ position: [0, 0, 10], fov: 65 }}
            gl={{ antialias: true, alpha: true, toneMapping: THREE.ACESFilmicToneMapping }}
            style={{ background: 'transparent' }}
        >
            <QuantumScene phase={phase} explode={explode} />
        </Canvas>
    );
}
