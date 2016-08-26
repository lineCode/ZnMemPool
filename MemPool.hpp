#pragma once
#ifndef __MEMPOOL_HPP_2015_12_28
#define __MEMPOOL_HPP_2015_12_28

// 一个简易内存池

namespace ZnMemPool
{
	// 使用链表来管理节点，并做了合包处理，减少碎片，各方面效率还不错
	class mem_pool_link_list
	{
	public:
		struct node_head_t
		{
			node_head_t*		pre_;
			node_head_t*		next_;
			node_head_t*		free_pre_;
			node_head_t*		free_next_;
			size_t				size_;
			bool				is_free_;
			mem_pool_link_list* allocate_;
		};
		typedef char					value_t;
	public:
		mem_pool_link_list(const size_t _init_size)
			: buf_(new value_t[_init_size])
			, size_(_init_size)
			, free_((node_head_t*) buf_)
			, count_(1)
			, node_head_size_(sizeof(node_head_t))
		{
			free_->size_ = _init_size > node_head_size_ ? _init_size - node_head_size_ : 0;
			free_->next_ = nullptr;
			free_->pre_ = nullptr;
			free_->free_next_ = nullptr;
			free_->free_pre_ = nullptr;
			free_->is_free_ = true;
			free_->allocate_ = this;
		}
		~mem_pool_link_list()
		{
			delete[] buf_;
		}
		const size_t		size() const {return size_;}
		const size_t		count() const {return count_;}
		value_t*			create(const size_t _size)
		{
			if (_size + node_head_size_ > size_)
				return nullptr;
			return __new_node(_size);
		}
		bool				release(value_t* _ptr)
		{
			if (!_ptr) return false;
			node_head_t* head = __get_head(_ptr);
			if (!head) return false;
			__add_free_node(head);
			__merger_free_node(head->pre_, head);
			__merger_free_node(head, head->next_);
			return true;
		}
	private:
		value_t*			__new_node(const size_t _size)
		{
			for (node_head_t* node = free_; node; node = node->free_next_)
			{
				if (_size > node->size_) continue;
				__split_free_node(node, _size);
				__del_free_node(node);
				return (value_t*)(node + 1);
			}
			return nullptr;
		}
		void				__split_free_node(node_head_t* _node, const size_t _size)
		{
			if (_node->size_ > node_head_size_ && _size < _node->size_ - node_head_size_)
			{
				size_t free_size = _node->size_ - node_head_size_ - _size;
				_node->size_ = _size;
				node_head_t* new_node = (node_head_t*)((value_t*)(_node + 1) + _size);
				new_node->size_ = free_size;
				new_node->next_ = _node->next_;
				new_node->pre_ = _node;
				new_node->free_next_ = _node->free_next_;
				new_node->free_pre_ = _node;
				new_node->allocate_ = this;
				new_node->is_free_ = true;
				if (_node->next_) 
					_node->next_->pre_ = new_node;
				if (_node->free_next_)
					_node->free_next_->free_pre_ = new_node;
				_node->next_ = new_node;
				_node->free_next_ = new_node;
				++count_;
			}
		}
		void				__merger_free_node(node_head_t* _pre, node_head_t* _next)
		{
			if (_pre && _next && _pre->is_free_ && _next->is_free_)
			{
				__del_free_node(_next);
				_pre->next_ = _next->next_;
				if (_next->next_) _next->next_->pre_ = _pre;
				_pre->size_ += node_head_size_ + _next->size_;
				--count_;
			}
		}
		void				__add_free_node(node_head_t* _new_node)
		{
			if (free_)
			{
				_new_node->free_next_ = free_;
				_new_node->free_pre_ = nullptr;
				_new_node->is_free_ = true;
				free_->free_pre_ = _new_node;
				free_ = _new_node;
			}
			else
			{
				free_ = _new_node;
				_new_node->free_pre_ = nullptr;
				_new_node->free_next_ = nullptr;
				_new_node->is_free_ = true;
			}
		}
		void				__del_free_node(node_head_t* _del_node)
		{
			if (free_ == _del_node)
			{
				free_ = _del_node->free_next_;
				if (free_) 
					free_->free_pre_ = nullptr;
			}
			else
			{
				if (_del_node->free_pre_) 
					_del_node->free_pre_->free_next_ = _del_node->free_next_;
				if (_del_node->free_next_) 
					_del_node->free_next_->free_pre_ = _del_node->free_pre_;
			}
			_del_node->free_pre_ = nullptr;
			_del_node->free_next_ = nullptr;
			_del_node->is_free_ = false;
		}
		node_head_t*		__get_head(value_t* _ptr)
		{
			if (_ptr && _ptr >= buf_ && _ptr < buf_ + size_) 
				return (node_head_t*)(_ptr - node_head_size_);
			return nullptr;
		}
	private:
		value_t*			buf_;				// 内存块
		size_t				size_;				// 内存块大小
		size_t				count_;				// 节点个数
		node_head_t*		free_;				// 空闲链表头
		const size_t		node_head_size_;	// 节点头结构大小
	};
	// RAII的自动加解锁类
	template<typename mutex_t>
	class auto_lock
	{
	public:
		typedef mutex_t  mutex_t;		
	public:
		auto_lock(mutex_t* _mutex) : mutex_(_mutex){mutex_->lock();}
		~auto_lock(){mutex_->unlock();}
	private:
		mutex_t*				mutex_;
	};
	// 内存池管理类
	template<typename allocate_t, typename mutex_t>
	class _base_mem_pool
	{
	public:
		typedef allocate_t									allocate_t;
		typedef typename allocate_t::value_t				value_t;
		typedef typename allocate_t::node_head_t			node_head_t;
		typedef mutex_t										mutex_t;
		struct mem_table_t
		{
			mem_table_t(const size_t _allocate_size)
				: allocate_(_allocate_size)
				, next_(nullptr)
			{}
			allocate_t		allocate_;
			mem_table_t*	next_;
		};
	public:
		_base_mem_pool(const size_t _allocate_size)
			: begin_(nullptr)
			, end_(nullptr)
			, count_(0)
			, node_head_size_(sizeof(node_head_t))
			, allocate_size_(_allocate_size)
		{}
		~_base_mem_pool()
		{
			if (begin_)
				__delete_mem_table(begin_);
		}
		value_t*			create(const size_t _size)
		{
			auto_lock<mutex_t> lock(&mutex_);
			value_t* pRet = nullptr;
			for (mem_table_t* p = begin_; p; p = p->next_)
			{
				pRet = p->allocate_.create(_size);
				if (pRet)
					return pRet;
			}
			__new_mem_table(_size > allocate_size_ ? _size : allocate_size_);
			return end_->allocate_.create(_size);
		}
		// VS2010不支持可变参模板，只写了目标类 0~3 个构造参数的创建方法
		template<typename class_t>
		class_t*			create()
		{
			value_t* p = create(sizeof(class_t));
			if (!p) return nullptr;
			return new (p) class_t;
		}
		template<typename class_t, typename t_param1>
		class_t*			create(t_param1 _p1)
		{
			value_t* p = create(sizeof(class_t));
			if (!p) return nullptr;
			return new (p) class_t(_p1);
		}
		template<typename class_t, typename t_param1, typename t_param2>
		class_t*			create(t_param1 _p1, t_param2 _p2)
		{
			value_t* p = create(sizeof(class_t));
			if (!p) return nullptr;
			return new (p) class_t(_p1, _p2);
		}
		template<typename class_t, typename t_param1, typename t_param2, typename t_param3>
		class_t*			create(t_param1 _p1, t_param2 _p2, t_param3 _p3)
		{
			value_t* p = create(sizeof(class_t));
			if (!p) return nullptr;
			return new (p) class_t(_p1, _p2, _p3);
		}
		bool				release(value_t* _ptr)
		{
			if (!_ptr) return false;
			auto_lock<mutex_t> lock(&mutex_);
			node_head_t* node_head = (node_head_t*)(_ptr - node_head_size_);
			return node_head->allocate_->release(_ptr);
		}
		template<typename class_t>
		bool				release(class_t* _ptr)
		{
			if (!_ptr) return false;
			_ptr->~class_t();
			return release((value_t*)_ptr);
		}
		const size_t		count() const {return count_;}
	private:
		void				__new_mem_table(const size_t _mem_table_size)
		{
			mem_table_t* new_mem_table = new mem_table_t(_mem_table_size);
			if (!begin_) 
				begin_ = new_mem_table;
			if (end_)
			{
				end_->next_ = new_mem_table;
				end_ = new_mem_table;
			}
			else
				end_ = begin_;
			++count_;
		}
		void				__delete_mem_table(mem_table_t* _mem_table)
		{
			if (_mem_table->next_)
				__delete_mem_table(_mem_table->next_);
			delete _mem_table;
		}
	private:
		mem_table_t*		begin_;
		mem_table_t*		end_;
		size_t				count_;
		const size_t		node_head_size_;
		const size_t		allocate_size_;
		mutex_t				mutex_;
	};
	// 关键段管理类
	class critical_section
	{
	public:
		critical_section(){::InitializeCriticalSection(&section_);}
		~critical_section(){::DeleteCriticalSection(&section_);}
		void lock(){EnterCriticalSection(&section_);}
		void unlock(){LeaveCriticalSection(&section_);}
	private:
		CRITICAL_SECTION		section_;
	};
	// 空的互斥类
	class none_mutex
	{
	public:
		void lock(){}
		void unlock(){}
	};
	// 基于关键段同步的内存池模板定义
	typedef _base_mem_pool<mem_pool_link_list, critical_section>						mem_pool;
	// 非线程安全的内存池模板定义
	typedef _base_mem_pool<mem_pool_link_list, none_mutex>								mem_pool_unsafe;
}

#endif