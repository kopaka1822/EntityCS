# EntityCS - C++ Entity Component System

## Overview

An Entity Component System (ECS) tries to seperate game logic from data.
By doing this it prevents deep class hierarchies (which are somewhat slow and not always easy to design).
The Idea is that every entity consists of several components (Transform, Health, Armor...) and the logic is performed
on every entity with a specific shema (position updates on every entity with Transform + Movement components).
The [Evolve your Hierarchy](http://cowboyprogramming.com/2007/01/05/evolve-your-heirachy/) article provides a solid overview of EC systems and why you should use them.

## Class Overview

If you don't know the [Tutorial](#tutorial), you probably want to go there first.

Note: TComponents... is a c++ 11 feature called variadic. It allows you to have a variable number of template arguments.

```c++
Manager<float, double> m1;
Manager<int, float, double, char> m2;
```

ManagerT, EntityT, ScriptT and SystemT will be used to refer to the corresponding Classname<TComponents...> template.

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
- `virtual methods:`  
  - `virtual ~Script()` 
  - `virtual void begin()` called when entity is spawned.  
  - `virtual void tick(float dt)` called every frame.  
- `helper methods`  
  - `EntityT& getEntity()` returns a reference to the currently processed entity of this script.  
  - `const EntityT& getEntity() const` 
  - `ManagerT& getManager() const`
  
### SystemT
- `virtual methods:`  
  - `virtual ~System()`
  - `virtual void initQueries(ManagerT& m)` called withing `Manager.start()`. Implement your `Manager.addQuery<>()` calls here.
  - `virtual void begin()` called within `Manager.start()` after initQueries().
  - `virtual void tick(float dt)` called every frame.  
- `helper methods`  
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

After you declared your components, the manager can be created.  
[class overview](#managert)

```c++
ecs::Manager<Transform, Movement, Shape> m;
m.start();
```

For convenience you may do something like this:

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
This is how you can work with the components:  
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
In order to do this you have to derive from the script class.
Particle script example:  
[class overview](#scriptt)

```c++
// particle script with custom lifetime
class ParticleScript : public ecs::Script<SYSTEM>
{
public:
	ParticleScript(float lifetime)
		:
	m_time(time)
	{}
	// in this case empty
	// will be called on entity spawn
	void begin() override
	{}
	// called every frame
	void tick(float dt) override
	{
		m_life -= dt;
		if(m_life < 0.0f)
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

This will add the particle script for a 5 second lifetime.
You can add multiple scripts to one Entity, they will be executed in the order they were added within the `Manager.tick(float dt)` call.
You can even share one script between multiple entities because you are passing a shared_ptr.

### Queries

At some point you probably want to perform some actions on your entities. You can easily do this using the Manager `m`.

```c++
// this will return a const std::vector<std::shared_ptr<ecs::Entity<SYSTEM>>>& to all entities with Transform and Movement components
const auto& myEnts = m.getEntsWith<Transform,Movement>();
// it is a constant reference because you should not change the vectors size, but you can change the entities within the vector
for(const auto& e : myEnts)
{
	// do some stuff..
	e->getComponent<Movement>().acceleration = 1.0f;
	// ...
}
```

However without adding the query cache to the manager, this would be somewhat slow because the vector will be created on function call.
It is more efficient to add a query cache to the manager before Manager.start() to optimize this call by caching the result of the function.

```c++
ecs::Manager<SYSTEM> m;
// add query to cache
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

You may also use the manager to perform per entity operations. To do that you have to pass a lambda function that will be performed per entity.
Adding the query is still required.

```c++
m.forEach([](ecs::Entity<System>& e)
{
	e.getComponent<Movement>().acceleration = 1.0f;
});
```

This may seem slower like the other approach but with compiler inlining this approach is exactly as fast as the previous.
A way to improve independent per entity actions for a bigger amount of entities is running this function on multiple threads.
With the manager this can be easily done:

```c++
m.forEachParallel([](ecs::Entity<System>& e)
{
	e.getComponent<Movement>().acceleration = 1.0f;
});
```

This method will apply the function on the first entity and measures the time needed to process one entity.
It will evaluate if execution on multiple cores will speed up this call based on: the amount of entities,
the measured time, the time until a thread starts and the number of cores. This will probably have some poor
performance in debug mode (because its monitoring several threads) but the release build should be faster.

### Adding Systems

If you have specific actions that should be performed on component groups you can declare Systems. Systems are used to organize your Code.

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

If you declare a `MovementSystem` that does the same, your code can be written like this:

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

Declaring the MovementSystem class looks like this:  
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
	virtual void tick(float dt) override
	{
		getManager().forEachParallel<Transform, Movement>([dt](ecs::Entity<SYSTEM>& e)
		{
			e.getComponent<Transform>().position += e.getComponent<Movement>().velocity * dt;
		});
	}
};
```