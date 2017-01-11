# EntityCS - C++ Entity Component System

## Overview

The main purpose of an "Entity Component System" (ECS) is to seperate game logic from data.
It thus prevents deep class hierarchies (which are somewhat slow and not always easy to design).
The idea is that every entity consists of several components (e.g. transform, health, armor...) and the logic is performed
on every entity in a specific pattern ( e.g. position updates on every entity with transform + movement components).
Check out the [Evolve your Hierarchy](http://cowboyprogramming.com/2007/01/05/evolve-your-heirachy/) article. It provides a solid overview of EC systems and explains in
further detail why you should definitely use them yourself.

## Requirements

The system is written in c++ 11   

tested with:
- Visual Studio 2015 (msvc)
- g++ (mingw)
- clang

## Class Overview

If you haven't read the [Tutorial](#tutorial), you may want to check that out first.

Note: `TComponents...` is a c++ 11 feature called variadic. It allows you to have a variable number of template arguments.

```c++
template<typename... TComponents>
class Manager;

Manager<float, double> m1;
Manager<int, float, double, char> m2;
```

`ManagerT`, `EntityT`, `ScriptT` and `SystemT` will be used to refer to the corresponding `Classname<TComponents...>` template.

### ManagerT
- `template<typename... TReq> void addQuery()`
- `void addSystem(std::shared_ptr<SystemT> s)`
- `void start()`
- `std::shared_ptr<EntityT> addEntity()`
- `template<typename... TReq> const std::vector<shared_ptr<EntityT>>& getEntsWith()`
- `void tick(float dt)`
- `template<typename... TReq, typename TFunctor> void forEach(TFunctor func)`
- `template<typename... TReq, typename TFunctor> void forEachParallel(TFunctor func)`

### EntityT
- `void kill()` this will remove the entity in the next `Manager.tick(float dt)` call.
- `bool isAlive() const` returns true if entity is alive.
- `size_t getID() const` returns the unique ID of the entity.
- `template<class T> T& addComponent()`
- `template<class T> bool hasComponent() const`
- `template<class T> T& getComponent()`
- `template<class T> const T& getComponent() const`
- `void addScript(shared_ptr<ScriptT> s)`
- `ManagerT& getManager() const`  

### ScriptT
- virtual methods:  
  - `virtual ~Script()` 
  - `virtual void begin()` called when entity is spawned.  
  - `virtual void tick(float dt)` called every frame.  
- helper methods:  
  - `EntityT& getEntity()` returns a reference to the currently processed entity of this script.  
  - `const EntityT& getEntity() const` 
  - `ManagerT& getManager() const`
  
### SystemT
- virtual methods:  
  - `virtual ~System()`
  - `virtual void initQueries(ManagerT& m)` called within `Manager.start()`. Implement your `Manager.addQuery<>()` calls here.
  - `virtual void begin()` called within `Manager.start()` after initQueries().
  - `virtual void tick(float dt)` called every frame.  
- helper methods: 
  - `ManagerT& getManager() const`

## Tutorial

### Declaring Components

Components should only hold data.
A components can be designed like this:

```c++
struct Transform 
{
	vec3 position;
	vec3 scale;
};

struct Movement 
{
	vec3 velocity;
	vec3 acceleration;
};

struct Shape 
{
	vec3 color;
};
```

### Initializing the Manager

After you have declared your components, the manager can be created.  
[class overview](#managert)

```c++
ecs::Manager<Transform, Movement, Shape> m;
m.start();
```

For convenience you may want to use this:

```c++
#define SYSTEM Transform, Movement, Shape

ecs::Manager<SYSTEM> m;
m.start()

while(1)
{
	float dt = functionToGetTimeDelta();
	m.tick(dt);
}
```

After the `start()` call you may add entities.
The `tick(float dt)` will kill or spawn requested entites and execute added Systems as well as entity scripts.

### Spawning Entites

To spawn an entity you require the manager `m`

```c++
auto myEnt = m.addEntity();
```

This will return an `std::shared_ptr<ecs::Entity<SYSTEM>>` to the newly allocated entity.
The following code describes how you can interact with the components:  
[class overview](#entityt)

```c++
auto myEnt = m.addEntity();
// adding component (standart construction)
myEnt->addComponent<Transform>();
// changing properties
myEnt->getComponent<Transform>().position = vec3(10.0f, 20.0f, 0.0f);

// adding component + initializing
myEnt->addComponent<Shape>().color = vec3(1.0f,0.0f,0.0f);

// testing component
if(myEnt->hasComponent<Shape>())
	std::cout << "I have a shape";
```

### Adding Scripts to Entites

You can attach scripts to entities to give them special behaviour.
In order to do so, you have to derive from the script class.
Particle script example:  
[class overview](#scriptt)

```c++
// particle script with custom lifetime
class ParticleScript : public ecs::Script<SYSTEM>
{
public:
	ParticleScript(float lifetime)
		:
	m_time(lifetime)
	{}
	// in this case empty
	// will be called on entity spawn
	void begin() override
	{}
	// called every frame
	void tick(float dt) override
	{
		m_time -= dt;
		if(m_time < 0.0f)
			// use getEntity() to acquire reference to current entity
			getEntity().kill();
	}
private:
	float m_time;
};
```

Attaching script to an entity:

```c++
auto myEnt = m.addEntity();
myEnt.addScript(std::make_shared<ParticleScript>(5.0f));
```

This will add the particle script with a 5 second lifetime.
You can add multiple scripts to one entity. They will be executed in the exact order they were added within the `Manager.tick(float dt)` call.
Since you pass a shared_ptr to a script, you may also share one script between multiple entities.

### Queries

At some point you probably want to interact with your entities. You can simply do this by using the manager `m`.

```c++
// this will return a const std::vector<std::shared_ptr<ecs::Entity<SYSTEM>>>& to all entities with Transform and Movement components
const auto& myEnts = m.getEntsWith<Transform,Movement>();
// it is a constant reference since you should not change the vectors size, but still allows you to change the entities within the vector.
for(const auto& e : myEnts)
{
	// do some stuff..
	e->getComponent<Movement>().acceleration = 1.0f;
	// ...
}
```

However, without caching the query, the vector would be created within the function call and thus be
restricted in performance. Caching can be achieved by using `Manager.addQuery<Components...>()` before calling
`Manager.start()`.

```c++
ecs::Manager<SYSTEM> m;
// cache query
m.addQuery<Transform,Movement>();
m.start()

while(1)
{
	float dt = functionToGetTimeDelta();
	m.tick(dt);
	
	// acquire cached query
	const auto& myEnts = m.getEntsWith<Transform,Movement>();
	// ...
}
```

You may also use the manager to perform operations on specific entities, in other words "per entity operations". To do so, you have to pass a lambda function that
will be called per entity. But still, adding the query is required for fast executing.

```c++
m.forEach<Transform,Movement>([](ecs::Entity<System>& e)
{
	e.getComponent<Movement>().acceleration = 1.0f;
});
```

It may seem slower than the other approach, but with function inlining it is exactly as fast as the previous one.
A way to improve independent per entity actions for a bigger amount of entities may just be achieved by running the function on multiple threads.
However, the Manager can easily work this out:

```c++
m.forEachParallel<Transform,Movement>([](ecs::Entity<System>& e)
{
	e.getComponent<Movement>().acceleration = 1.0f;
});
```

This specific method applies the function on the first entity and measures the time needed to process one entity.
Based on the amount of entities, the measured time, the time until a thread starts and the number of cores, this method will evaluate whether the execution
on multiple cores can speed up the call. It might have some poor performance in debug mode (because its monitoring several threads), but the release build will 
definitely improve on this matter.

### Adding Systems

If you have specific actions that should be performed on component groups, you can simply declare Systems. Systems are used to organize your Code.

Position updates without System:

```c++
#define SYSTEM Transform,Movement,Shape

ecs::Manager<SYSTEM> m;
m.addQuery<Transform, Movement>()
m.start()

// code where entites are added

while(1)
{
	float dt = functionToGetTimeDelta();
	m.tick(dt);
	
	m.forEachParallel<Transform, Movement>([dt](ecs::Entity<SYSTEM>& e)
	{
		// position = velocity * dt;
		e.getComponent<Transform>().position += e.getComponent<Movement>().velocity * dt;
	});
}
```

If you declare a `MovementSystem` that does the same, your code can be written just like that:

```c++
ecs::Manager<SYSTEM> m;
// Note: all addSystem() calls must be made before m.start();
m.addSystem(std::make_shared<MovementSystem>());
m.start()

// code where entites are added

while(1)
{
	float dt = functionToGetTimeDelta();
	// System code will be performed within tick
	m.tick(dt);
}
```

Declaring the `MovementSystem` class looks like this:  
[class overview](#systemt)

```c++
// derive from ecs::System<SYSTEM>
public MovementSystem : public ecs::System<SYSTEM>
{
public:
	// which queries will be used by this system?
	void initQueries(ManagerT& m) override
	{
		m.addQuery<Transform, Movement>();
	}
	// will be executed within Manager.start();
	void begin() override
	{
		// can be used to spawn entities
	}
	// will be executed every frame
	void tick(float dt) override
	{
		getManager().forEachParallel<Transform, Movement>([dt](ecs::Entity<SYSTEM>& e)
		{
			e.getComponent<Transform>().position += e.getComponent<Movement>().velocity * dt;
		});
	}
};
```