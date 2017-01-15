#pragma once
#include <tuple>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <cassert>
#include <mutex>

namespace ecs
{
	using std::shared_ptr;
	using std::make_shared;
	using SystemKeyT = uint64_t;

	template<typename... TComponents>
	class Manager;

	template<typename... TComponents>
	class Entity;

	template<typename... TComponents>
	class Script
	{
	public:
		using EntityT = Entity<TComponents...>;
		using ManagerT = Manager<TComponents...>;
		friend EntityT;

		virtual ~Script() = default;
		// called on game start or spawn
		virtual void begin() {}
		// called every frame
		virtual void tick(float dt) {}

	protected:
		EntityT& getEntity()
		{
			assert(m_curEntity);
			return *m_curEntity;
		}
		const EntityT& getEntity() const
		{
			assert(m_curEntity);
			return *m_curEntity;
		}
		ManagerT& getManager() const
		{
			assert(m_manager);
			return *m_manager;
		}
	private:
		EntityT* m_curEntity = nullptr;
		ManagerT* m_manager = nullptr;
	};

	template<typename... TComponents>
	class System
	{
	public:
		using EntityT = Entity<TComponents...>;
		using ManagerT = Manager<TComponents...>;
		friend ManagerT;

		virtual ~System() = default;

		virtual void initQueries(ManagerT& m) {}
		virtual void begin() {}
		virtual void tick(float dt) {}
	protected:
		ManagerT& getManager() const
		{
			assert(m_manager);
			return *m_manager;
		}
	private:
		ManagerT* m_manager = nullptr;
	};

	template<typename... TComponents>
	class Entity
	{
	public:
		using ManagerT = Manager<TComponents...>;
		using ScriptT = Script<TComponents...>;
		friend ManagerT;

		void kill() noexcept
		{
			m_alive = false;
		}
		bool isAlive() const noexcept
		{
			return m_alive;
		}
		size_t getID() const
		{
			return m_id;
		}
		template<class T>
		T& addComponent()
		{
			assert(!m_componentsAdded);
			static const size_t slot = m_manager->template getComponentIndex<T>();
			m_componentFlags |= SystemKeyT(1) << SystemKeyT(slot);
			return getComponent<T>();
		}
		template<class T>
		bool hasComponent() const
		{
			return ((SystemKeyT(1) << (m_manager->template getComponentIndex<T>())) & m_componentFlags) != 0;
		}
#ifndef _MSC_BUILD
		template<class T>
		T& getComponent()
		{
			assert(hasComponent<T>());
			return std::get<ManagerT::template getComponentIndex<T>()>(m_components);
		}
		template<class T>
		const T& getComponent() const
		{
			assert(hasComponent<T>());
			return std::get<ManagerT::template getComponentIndex<T>()>(m_components);
		}
#else
		template<class T>
		T& getComponent()
		{
			assert(hasComponent<T>());
			return _getComponent<T>(m_components);
		}
		template<class T>
		const T& getComponent() const
		{
			assert(hasComponent<T>());
			return _getComponent<T>(m_components);
		}
#endif
		void addScript(shared_ptr<ScriptT> s)
		{
			assert(!m_componentsAdded);
			assert(s);
			assert(m_manager);
			s->m_manager = m_manager;
			s->m_curEntity = this;
			m_scripts.push_back(s);
		}
		ManagerT& getManager() const
		{
			assert(m_manager);
			return *m_manager;
		}
	private:
#ifdef _MSC_BUILD
		template<typename  T, typename... TComps>
		static constexpr T& _getComponent(std::tuple<T, TComps...>& t)
		{
			return std::get<0>(t);
		}
		template<typename  T, typename... TComps>
		static constexpr T& _getComponent(std::tuple<TComps...>& t)
		{
			return _getComponent<T>(t._Get_rest());
		}
		template<typename T>
		static constexpr T& _getComponent(std::tuple<>&)
		{
			// ReSharper disable once CppStaticAssertFailure
			static_assert(false, "component type does not exist in this system");
			return *reinterpret_cast<T*>(nullptr);
		}
		template<typename  T, typename... TComps>
		static constexpr const T& _getComponent(const std::tuple<T, TComps...>& t)
		{
			return std::get<0>(t);
		}
		template<typename  T, typename... TComps>
		static constexpr const T& _getComponent(const std::tuple<TComps...>& t)
		{
			return _getComponent<T>(t._Get_rest());
		}
		template<typename T>
		static constexpr const T& _getComponent(const std::tuple<>&)
		{
			// ReSharper disable once CppStaticAssertFailure
			static_assert(false, "component type does not exist in this system");
			return *reinterpret_cast<T*>(nullptr);
		}
#endif
		void runScript(float dt)
		{
			assert(hasScript());
			for (auto s : m_scripts)
			{
				s->m_curEntity = this;
				s->tick(dt);
				//s->m_curEntity = nullptr;
			}
		}
		void runStartupScript()
		{
			if (hasScript())
			{
				for (auto s : m_scripts)
				{
					s->m_curEntity = this;
					s->begin();
					//s->m_curEntity = nullptr;
				}
			}
		}
		bool hasScript() const
		{
			return m_scripts.size() != 0;
		}
	private:
		bool m_alive = true;
		bool m_componentsAdded = false;
		size_t m_id = -1;
		ManagerT* m_manager = nullptr;
		std::tuple<TComponents...> m_components;
		SystemKeyT m_componentFlags = 0; // bitflag of used components
		static_assert(std::tuple_size<std::tuple<TComponents...>>::value <= 64, "only up to 64 components are supported");
		std::vector<shared_ptr<ScriptT>> m_scripts;
	};

	template<typename... TComponents>
	class Manager
	{
		using TimeT = long long;
		enum class States
		{
			Init,
			Running
		};
	public:
		using EntityT = Entity<TComponents...>;
		using SystemT = System<TComponents...>;
		friend Entity<TComponents...>;

		Manager()
		{
			m_entities.reserve(1024);
			m_queries.reserve(64);
			m_freshEntities.reserve(1024);
			m_tempCachedQueries.reserve(1024);

			// measure time till a thread start for parallel execution
			std::chrono::high_resolution_clock clk;
			auto start = clk.now();
			auto end = start;
			std::thread t1([&end, &clk]()
			{
				end = clk.now();
			});
			t1.join();
			m_timeTillThreadStarts =
				TimeT(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

			// available Threads
			m_nThreads = std::thread::hardware_concurrency();
			// its better for the system to run core-1 threads, so the system has one thread for itself
			m_nThreads = m_nThreads > 3 ? m_nThreads - 1 : m_nThreads;
		}
		template<typename... TReq>
		void addQuery()
		{
			assert(m_state == States::Init);
			// add systems before adding entities
			static const std::tuple<TReq...> dummy;
			auto binaryKey = getComponentMask(0, dummy);
			// is system already added?
			for (const auto& s : m_queries)
			{
				// System already added!
				if (s.first == binaryKey)
					return;
			}
			m_queries.push_back(std::make_pair(binaryKey, std::vector<shared_ptr<EntityT>>()));
			m_queries.back().second.reserve(1024);
		}
		void addSystem(shared_ptr<SystemT> s)
		{
			assert(m_state == States::Init);
			s->m_manager = this;
			m_systems.push_back(s);
		}
		shared_ptr<EntityT> addEntity()
		{
			assert(m_state == States::Running);
			shared_ptr<EntityT> e = make_shared<EntityT>();
			e->m_manager = this;
			std::lock_guard<std::mutex> g(m_muEntityAdd);
			{
				e->m_id = m_curID++;
				// queue for adding (to prevent iterator lost when adding whilst iterating through entities)
				m_freshEntities.push_back(e);
			}
			if (e->hasScript())
				e->runStartupScript();
			return e;
		}
		template<typename... TReq>
		const std::vector<shared_ptr<EntityT>>& getEntsWith()
		{
			assert(m_state == States::Running);
			static const std::tuple<TReq...> dummy;
			auto binaryKey = getComponentMask(0, dummy);

			// get binary key from system
			for (auto& s : m_queries)
				if (s.first == binaryKey)
					return s.second;

			// no cached system available...
			{
				std::vector<shared_ptr<EntityT>> res;
				res.reserve(m_entities.size());
				for (const auto& e : m_entities)
				{
					if ((binaryKey & e->m_componentFlags) == binaryKey)
						res.push_back(e);
				}
				m_tempCachedQueries.push_back(move(res));
				return m_tempCachedQueries.back();
			}
		}
		void tick(float dt)
		{
			assert(m_state == States::Running);
			// remove dead entities + add entities with missing components
			m_tempCachedQueries.resize(0);

			// remove dead entities:
			bool scriptRemoved = false;
			SystemKeyT removedComponments = 0;
			if (removeDeadEntities(m_entities, removedComponments, scriptRemoved))
			{
				// probably some dead entites in here
				for (auto& q : m_queries)
				{
					if(q.first & removedComponments)
					{
						// only remove entities if at least one component from the query was removed
						removeDeadEntities(q.second);
					}
				}
			}

			// add new components
			if (m_freshEntities.size())
			{
				for (auto& e : m_freshEntities)
				{
					assert(e->m_componentsAdded == false);
					if (e->isAlive())
					{
						// run startup script
						e->runStartupScript();
						e->m_componentsAdded = true;
						m_entities.push_back(e);
						// generate component key
						auto entKey = getComponentKeyFromEntity(*e);
						for (auto& s : m_queries)
						{
							if ((s.first & entKey) == s.first)
								s.second.push_back(e);
						}
					}
				}
				m_freshEntities.resize(0);
			}

			// run systems
			for (auto& s : m_systems)
				s->tick(dt);

			// run scripts for entities
			for (auto& e : m_entities)
				if (e->hasScript())
					e->runScript(dt);
		}
		template<typename... TReq, typename TFunctor>
		void forEach(TFunctor func)
		{
			assert(m_state == States::Running);
			for (auto& e : getEntsWith<TReq...>())
				func(*e);
		}
		template<typename... TReq, typename TFunctor>
		void forEachParallel(TFunctor func)
		{
			assert(m_state == States::Running);
			auto& vec = getEntsWith<TReq...>();
			if (!vec.size())
				return;
			auto it = vec.begin();
			const auto end = vec.end();

			if (vec.size() > m_nThreads * 4)
			{
				// only if its really worth
				// calculate time for one iteration
				auto tstart = std::chrono::high_resolution_clock::now();
				func(*(*it));
				++it;
				auto tend = std::chrono::high_resolution_clock::now();
				TimeT duration =
					TimeT(std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tstart).count());

				// calculate if splitting is worth
				size_t step = (vec.size() - 1) / m_nThreads;
				TimeT timeWithoutThreads = TimeT(vec.size() - 1) * duration;
				TimeT timeWithThreads = TimeT(step) * duration + m_timeTillThreadStarts;

				if (timeWithThreads < timeWithoutThreads)
				{
					// execute parallel
					std::vector<std::thread> threads;
					threads.reserve(m_nThreads);
					for (size_t i = 0; i < m_nThreads - 1; i++)
					{
						threads.emplace_back([it, step, func]()
						{
							auto i = it; // because of capture thing
							auto end = it + step;
							while (i != end)
							{
								func(*(*i));
								++i;
							}
						});
						it += step;
					}
					// execute last on current threads
					while (it != end)
					{
						func(*(*it));
						++it;
					}

					for (auto& t : threads)
						t.join();
					return;
				}
			}
			// execute on single thread
			for (; it != end; ++it)
				func(*(*it));
		}
		void start()
		{
			assert(m_state == States::Init);
			// add queries
			for (auto& s : m_systems)
				s->initQueries(*this);

			m_state = States::Running;

			// init systems
			for (auto& s : m_systems)
				s->begin();
		}
	private:
		template<class T>
		static constexpr size_t getComponentIndex()
		{
			// ReSharper disable once CppCStyleCast
			return _getComponentIndex(0, (T*)(nullptr), std::tuple<TComponents...>());
		}
		template<typename T, typename... TComps>
		static constexpr size_t _getComponentIndex(size_t slot, T* dummy, const std::tuple<T, TComps...>& t)
		{
			return slot;
		}
		template<typename T, typename U, typename... TComps>
		static constexpr size_t _getComponentIndex(size_t slot, T* dummy, const std::tuple<U, TComps...>& t)
		{
			return _getComponentIndex(slot + 1, dummy, std::tuple<TComps...>());
		}
		template<typename T>
		static constexpr size_t _getComponentIndex(size_t slot, const T& dummy, const std::tuple<>&)
		{
#ifdef _MSC_BUILD
			// ReSharper disable once CppStaticAssertFailure
			static_assert(false, "component type does not exist in this system");
#endif
			return -1;
		}
		template<typename T, typename... TComps>
		static constexpr SystemKeyT getComponentMask(SystemKeyT key, const std::tuple<T, TComps...>& t)
		{
			return getComponentMask(key | SystemKeyT(1) << SystemKeyT(getComponentIndex<T>()), std::tuple<TComps...>());
		}
		static constexpr SystemKeyT getComponentMask(SystemKeyT key, const std::tuple<>& t)
		{
			return key;
		}
		static constexpr SystemKeyT getComponentKeyFromEntity(const EntityT& e)
		{
			return e.m_componentFlags;
		}
		/*
		this will remove all dead entites in the vector with runtime O(n)
		*/
		void removeDeadEntities(std::vector<shared_ptr<EntityT>>& v)
		{
			// idea:

			// find first dead entity from left
			// find first living entity from right
			// <- swap ->
			// shrink list
			if (!v.size()) return;

			size_t left = 0;
			size_t right = v.size() - 1;
			while (left <= right)
			{
				// is dead?
				if (!v[left]->isAlive())
				{
					// search first dead in right
					while (right > left)
					{
						if (v[right]->isAlive())
							break;
						right--;
						v.pop_back();
					}
					if (right > left)
					{
						// do the swap
						std::swap(v[left], v[right]);
						right--;
					}
					v.pop_back();
				}
				left++;
			}
		}
		/*
		this will remove all dead entites in the vector with runtime O(n)
		returns true if dead entities were found
		-> false if nothing was changed

		rflag will indicate all removed components from entities
		rscript will indicate if at least one removed entity had a script
		*/
		bool removeDeadEntities(std::vector<shared_ptr<EntityT>>& v, SystemKeyT& rflag, bool& rscript)
		{
			// idea:

			// find first dead entity from left
			// find first living entity from right
			// <- swap ->
			// shrink list
			if (!v.size()) return false;

			size_t left = 0;
			size_t right = v.size() - 1;
			size_t startSize = v.size();
			rflag = 0;
			rscript = false;
			while (left <= right)
			{
				// is dead?
				if (!v[left]->isAlive())
				{
					rflag |= v[left]->m_componentFlags;
					rscript = rscript || v[left]->hasScript();
					// search first dead in right
					while (right > left)
					{
						if (v[right]->isAlive())
							break;
						right--;
						v.pop_back();
					}
					if (right > left)
					{
						// do the swap
						std::swap(v[left], v[right]);
						right--;
					}
					v.pop_back();
				}
				left++;
			}
			return startSize != v.size();
		}
	private:
		// all entities
		std::vector<shared_ptr<EntityT>> m_entities;
		// entities that were not added to any system
		std::vector<shared_ptr<EntityT>> m_freshEntities;
		std::vector<std::vector<shared_ptr<EntityT>>> m_tempCachedQueries;
		size_t m_curID = 0;
		std::vector<std::pair<SystemKeyT, std::vector<shared_ptr<EntityT>>>> m_queries;
		std::vector<shared_ptr<SystemT>> m_systems;
		States m_state = States::Init;
		TimeT m_timeTillThreadStarts = 0;
		size_t m_nThreads = 0;
		std::mutex m_muEntityAdd;
	};
}
