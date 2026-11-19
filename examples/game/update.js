function update(deltaTime) {
    const speed = 120;
    const distance = calculateDistance(speed, deltaTime);
    Engine.log(`distance=${distance}`);
    return distance;
}
