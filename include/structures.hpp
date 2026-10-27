#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace journey {
template <typename T> class LinkedList {
    struct Node { T value; std::unique_ptr<Node> next; explicit Node(const T& v) : value(v) {} };
    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
public:
    LinkedList() = default;
    LinkedList(const LinkedList& other) { other.for_each([&](const T& v){push_back(v);}); }
    LinkedList(LinkedList&& other) noexcept { swap(other); }
    LinkedList& operator=(LinkedList other) { swap(other); return *this; }
    ~LinkedList() { while(head_) { auto node=std::move(head_); head_=std::move(node->next); } }
    void swap(LinkedList& other) noexcept { head_.swap(other.head_); std::swap(tail_,other.tail_); std::swap(size_,other.size_); }
    void push_back(const T& value) {
        auto node=std::make_unique<Node>(value); auto* raw=node.get();
        if(tail_) tail_->next=std::move(node); else head_=std::move(node);
        tail_=raw; ++size_;
    }
    std::size_t size() const { return size_; }
    template <typename F> void for_each(F f) { for(auto* p=head_.get();p;p=p->next.get()) f(p->value); }
    template <typename F> void for_each(F f) const { for(const auto* p=head_.get();p;p=p->next.get()) f(p->value); }
    template <typename F> T* find(F f) { for(auto* p=head_.get();p;p=p->next.get()) if(f(p->value))return &p->value; return nullptr; }
    template <typename F> const T* find(F f) const { for(const auto* p=head_.get();p;p=p->next.get()) if(f(p->value))return &p->value; return nullptr; }
};
template <typename T> class LinkedQueue {
    struct Node { T value; std::unique_ptr<Node> next; explicit Node(const T& v): value(v) {} };
    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
public:
    LinkedQueue() = default;
    LinkedQueue(const LinkedQueue& other) { for(const auto& v:other.values())push(v); }
    LinkedQueue(LinkedQueue&& other) noexcept { swap(other); }
    LinkedQueue& operator=(LinkedQueue other) { swap(other);return *this; }
    ~LinkedQueue() { while(head_) { auto node=std::move(head_); head_=std::move(node->next); } }
    void swap(LinkedQueue& other) noexcept { head_.swap(other.head_);std::swap(tail_,other.tail_);std::swap(size_,other.size_); }
    void push(const T& value) {
        auto node=std::make_unique<Node>(value);auto* raw=node.get();
        if(tail_)tail_->next=std::move(node);else head_=std::move(node);
        tail_=raw;++size_;
    }
    T pop() {
        if(!head_)throw std::out_of_range("Waiting queue is empty");
        auto node=std::move(head_);T value=node->value;head_=std::move(node->next);--size_;if(!head_)tail_=nullptr;return value;
    }
    bool remove(const T& value) {
        auto* link=&head_;Node* previous=nullptr;
        while(*link) {
            if((*link)->value==value) { if(link->get()==tail_)tail_=previous;auto node=std::move(*link);*link=std::move(node->next);--size_;return true; }
            previous=link->get();link=&((*link)->next);
        }
        return false;
    }
    std::size_t size() const { return size_; }
    bool empty() const { return size() == 0; }
    std::vector<T> values() const { std::vector<T> out;out.reserve(size_);for(auto* p=head_.get();p;p=p->next.get())out.push_back(p->value);return out; }
};
class MinHeap {
    std::vector<int> data_;
    void up(std::size_t at) {
        while(at>0) {const auto parent=(at-1)/2;if(data_[parent]<=data_[at])break;std::swap(data_[parent],data_[at]);at=parent;}
    }
    void down(std::size_t at) {
        while(at*2+1<data_.size()) {auto child=at*2+1;if(child+1<data_.size() && data_[child+1]<data_[child])++child;if(data_[at]<=data_[child])break;std::swap(data_[at],data_[child]);at=child;}
    }
public:
    void push(int value) { data_.push_back(value);up(data_.size()-1); }
    int pop() { if(data_.empty())throw std::out_of_range("No seats available");const int value=data_[0];remove(value);return value; }
    bool remove(int value) {
        std::size_t at=0;while(at<data_.size() && data_[at]!=value)++at;if(at==data_.size())return false;
        data_[at]=data_.back();data_.pop_back();
        if(at<data_.size()) {if(at>0 && data_[at]<data_[(at-1)/2])up(at);else down(at);}return true;
    }
    std::size_t size() const { return data_.size(); }
    bool empty() const { return size() == 0; }
};
}
