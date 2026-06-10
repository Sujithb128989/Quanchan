/*
 * Copyright (C) 2026 QuanChan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import { useRef, useMemo, useEffect } from 'react';
import { Canvas, useFrame, useLoader, useThree } from '@react-three/fiber';
import { TextureLoader } from 'three';
import * as THREE from 'three';

// Stars field
function Stars({ count = 8000 }: { count?: number }) {
    const positions = useMemo(() => {
        const pos = new Float32Array(count * 3);
        for (let i = 0; i < count; i++) {
            const theta = Math.random() * Math.PI * 2;
            const phi = Math.acos(2 * Math.random() - 1);
            const r = 40 + Math.random() * 160;
            pos[i * 3] = r * Math.sin(phi) * Math.cos(theta);
            pos[i * 3 + 1] = r * Math.sin(phi) * Math.sin(theta);
            pos[i * 3 + 2] = r * Math.cos(phi);
        }
        return pos;
    }, [count]);

    const colors = useMemo(() => {
        const col = new Float32Array(count * 3);
        for (let i = 0; i < count; i++) {
            const t = Math.random();
            if (t < 0.3) { col[i * 3] = 1; col[i * 3 + 1] = 0.27; col[i * 3 + 2] = 0; }
            else if (t < 0.5) { col[i * 3] = 0; col[i * 3 + 1] = 0.94; col[i * 3 + 2] = 1; }
            else { col[i * 3] = 0.9; col[i * 3 + 1] = 0.9; col[i * 3 + 2] = 0.9; }
        }
        return col;
    }, [count]);

    const ref = useRef<THREE.Points>(null);
    useFrame((_, delta) => {
        if (ref.current) {
            ref.current.rotation.y += delta * 0.008;
            ref.current.rotation.x += delta * 0.002;
        }
    });

    return (
        <points ref={ref}>
            <bufferGeometry>
                <bufferAttribute attach="attributes-position" count={count} array={positions} itemSize={3} args={[positions, 3]} />
                <bufferAttribute attach="attributes-color" count={count} array={colors} itemSize={3} args={[colors, 3]} />
            </bufferGeometry>
            <pointsMaterial size={0.18} vertexColors sizeAttenuation transparent opacity={0.9} />
        </points>
    );
}

// Fire particles
function FireParticles({ count = 200 }: { count?: number }) {
    const meshRef = useRef<THREE.Points>(null);
    const data = useMemo(() => {
        const positions = new Float32Array(count * 3);
        const velocities = new Float32Array(count * 3);
        const lifetimes = new Float32Array(count);
        for (let i = 0; i < count; i++) {
            positions[i * 3] = (Math.random() - 0.5) * 8;
            positions[i * 3 + 1] = (Math.random() - 0.5) * 8;
            positions[i * 3 + 2] = (Math.random() - 0.5) * 2;
            velocities[i * 3] = (Math.random() - 0.5) * 0.02;
            velocities[i * 3 + 1] = Math.random() * 0.04;
            velocities[i * 3 + 2] = (Math.random() - 0.5) * 0.01;
            lifetimes[i] = Math.random();
        }
        return { positions, velocities, lifetimes };
    }, [count]);

    useFrame((_, delta) => {
        if (!meshRef.current) return;
        const pos = meshRef.current.geometry.attributes.position.array as Float32Array;
        for (let i = 0; i < count; i++) {
            data.lifetimes[i] += delta * 0.4;
            pos[i * 3] += data.velocities[i * 3];
            pos[i * 3 + 1] += data.velocities[i * 3 + 1];
            pos[i * 3 + 2] += data.velocities[i * 3 + 2];
            if (data.lifetimes[i] > 1) {
                pos[i * 3] = (Math.random() - 0.5) * 6;
                pos[i * 3 + 1] = -4;
                pos[i * 3 + 2] = (Math.random() - 0.5) * 2;
                data.lifetimes[i] = 0;
            }
        }
        meshRef.current.geometry.attributes.position.needsUpdate = true;
    });

    return (
        <points ref={meshRef}>
            <bufferGeometry>
                <bufferAttribute attach="attributes-position" count={count} array={data.positions} itemSize={3} args={[data.positions, 3]} />
            </bufferGeometry>
            <pointsMaterial size={0.06} color="#ffd700" transparent opacity={0.7} sizeAttenuation />
        </points>
    );
}

// Phoenix hologram
function PhoenixHologram({ texture, isMobile }: { texture: THREE.Texture; isMobile: boolean }) {
    const meshRef = useRef<THREE.Mesh>(null);
    const { viewport } = useThree();
    const dimensions = useMemo(() => {
        const image = texture.image as { width?: number; height?: number } | undefined;
        const ratio = image?.width && image?.height ? image.width / image.height : 1;
        const maxWidth = isMobile ? viewport.width * 0.82 : Math.min(12, viewport.width * 0.7);
        const maxHeight = isMobile ? viewport.height * 0.52 : Math.min(12, viewport.height * 0.72);

        let width = maxWidth;
        let height = width / ratio;
        if (height > maxHeight) {
            height = maxHeight;
            width = height * ratio;
        }

        return { width, height };
    }, [isMobile, texture.image, viewport.height, viewport.width]);

    useFrame((state) => {
        if (meshRef.current) {
            meshRef.current.rotation.y = Math.sin(state.clock.elapsedTime * 0.3) * 0.3;
            const s = 1 + Math.sin(state.clock.elapsedTime * 0.7) * 0.05;
            meshRef.current.scale.setScalar(s);
            (meshRef.current.material as THREE.MeshBasicMaterial).opacity =
                0.5 + Math.sin(state.clock.elapsedTime * 1.2) * 0.15;
        }
    });

    return (
        <mesh ref={meshRef} position={[0, isMobile ? -0.9 : -0.25, 0]}>
            <planeGeometry args={[dimensions.width, dimensions.height]} />
            <meshBasicMaterial map={texture} transparent opacity={0.55} side={THREE.DoubleSide} depthWrite={false} />
        </mesh>
    );
}

// Camera zoom controller â€” drives camera.position.z towards target
function CameraZoom({ zooming }: { zooming: boolean }) {
    const { camera } = useThree();
    const targetZ = useRef(20);
    const speed = useRef(0);

    useEffect(() => {
        if (zooming) {
            targetZ.current = -30; // dive deep past the phoenix
            speed.current = 0;
        } else {
            targetZ.current = 20;
            speed.current = 0;
        }
    }, [zooming]);

    useFrame((_, delta) => {
        const dist = targetZ.current - camera.position.z;
        if (Math.abs(dist) > 0.01) {
            // Ease-in acceleration for dramatic zoom
            speed.current = Math.min(speed.current + delta * 40, 60);
            camera.position.z += Math.sign(dist) * speed.current * delta;
        }
    });

    return null;
}

function Scene({ zooming }: { zooming: boolean }) {
    const { size } = useThree();
    const isMobile = size.width < 768;
    const texture = useLoader(TextureLoader, '/phoenix.png');
    return (
        <>
            <ambientLight intensity={0.1} />
            <Stars count={isMobile ? 4200 : 8000} />
            <FireParticles count={isMobile ? 140 : 250} />
            <PhoenixHologram texture={texture} isMobile={isMobile} />
            <CameraZoom zooming={zooming} />
        </>
    );
}

interface PhoenixBackgroundProps {
    zooming?: boolean;
    opacity?: number;
}

export default function PhoenixBackground({ zooming = false, opacity = 0.95 }: PhoenixBackgroundProps) {
    const isMobile = typeof window !== 'undefined' && window.matchMedia('(max-width: 768px)').matches;

    return (
        <div className="fixed inset-0" style={{ opacity, zIndex: 0, pointerEvents: 'none' }}>
            <Canvas
                dpr={[1, 1.5]}
                camera={{ position: [0, isMobile ? -0.15 : 0, isMobile ? 17 : 20], fov: isMobile ? 78 : 70 }}
                gl={{ antialias: true, alpha: true }}
                style={{ background: 'transparent' }}
            >
                <Scene zooming={zooming} />
            </Canvas>
        </div>
    );
}
