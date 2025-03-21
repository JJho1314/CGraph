/***************************
@Author: Chunel
@Contact: chunel@foxmail.com
@File: GElement.cpp
@Time: 2021/6/1 10:13 下午
@Desc: 
***************************/

#include "GElement.h"

CGRAPH_NAMESPACE_BEGIN

GElement::GElement() {
    element_type_ = GElementType::ELEMENT;
}


GElement::~GElement() {
    CGRAPH_DELETE_PTR(aspect_manager_)
    for (auto& param : local_params_) {
        CGRAPH_DELETE_PTR(param.second)    // 依次删除本地的参数信息
    }
}


CStatus GElement::beforeRun() {
    CGRAPH_FUNCTION_BEGIN
    this->done_ = false;
    this->left_depend_ = dependence_.size();

    CGRAPH_FUNCTION_END
}


CStatus GElement::afterRun() {
    CGRAPH_FUNCTION_BEGIN

    for (auto& element : this->run_before_) {
        element->left_depend_--;
    }
    this->done_ = true;

    CGRAPH_FUNCTION_END
}


GElementPtr GElement::setName(const std::string& name) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)
    this->name_ = name.empty() ? this->session_ : name;

    // 设置name信息的时候，顺便给 aspect_manager_ 一起设置了
    if (aspect_manager_) {
        aspect_manager_->setName(name_);
    }
    return this;
}


GElement* GElement::setLoop(CSize loop) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)

    this->loop_ = loop;
    return this;
}


GElement* GElement::setLevel(CLevel level) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)

    this->level_ = level;
    return this;
}


GElement* GElement::setVisible(CBool visible) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)

    this->visible_ = visible;
    return this;
}


GElement* GElement::setBindingIndex(CIndex index) {
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)

    this->binding_index_ = index;
    return this;
}


CBool GElement::isRunnable() const {
    return 0 >= this->left_depend_ && !this->done_;
}


CBool GElement::isLinkable() const {
    return this->linkable_;
}


CStatus GElement::addDependGElements(const GElementPtrSet& elements) {
    CGRAPH_FUNCTION_BEGIN
    for (GElementPtr cur: elements) {
        CGRAPH_ASSERT_NOT_NULL(cur)
        if (this == cur) {
            continue;
        }

        cur->run_before_.insert(this);
        this->dependence_.insert(cur);
    }

    this->left_depend_ = this->dependence_.size();
    CGRAPH_FUNCTION_END
}


CStatus GElement::setElementInfo(const GElementPtrSet& dependElements,
                                 const std::string& name,
                                 CSize loop,
                                 GParamManagerPtr paramManager,
                                 GEventManagerPtr eventManager) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_INIT(false)

    this->setName(name)->setLoop(loop);
    param_manager_ = paramManager;
    event_manager_ = eventManager;
    status = this->addDependGElements(dependElements);
    CGRAPH_FUNCTION_END
}


CStatus GElement::doAspect(const GAspectType& aspectType, const CStatus& curStatus) {
    CGRAPH_FUNCTION_BEGIN

    // 如果切面管理类为空，或者未添加切面，直接返回
    if (this->aspect_manager_
        && 0 != this->aspect_manager_->getSize()) {
        status = aspect_manager_->reflect(aspectType, curStatus);
    }

    CGRAPH_FUNCTION_END
}


CStatus GElement::fatProcessor(const CFunctionType& type) {
    CGRAPH_FUNCTION_BEGIN

    if (unlikely(!visible_)) {
        /**
         * 如果当前的 element 因为被remove等原因，变成 不可见的状态
         * 则不运行。但不是实际删除当前节点信息
         */
        CGRAPH_FUNCTION_END
    }

    try {
        switch (type) {
            case CFunctionType::RUN: {
                for (CSize i = 0; i < this->loop_ && GElementState::CANCEL != cur_state_.load(); i++) {
                    /** 执行带切面的run方法 */
                    status = doAspect(GAspectType::BEGIN_RUN);
                    CGRAPH_FUNCTION_CHECK_STATUS
                    do {
                        status = run();
                        /**
                         * 在run结束之后，首先需要判断一下是否进入yield状态了。
                         * 接下来，如果状态是ok的，并且被条件hold住，则循环执行
                         * 默认所有element的isHold条件均为false，即不hold，即执行一次
                         * 可以根据需求，对任意element类型，添加特定的isHold条件
                         * 并且没有被退出
                         * */
                    } while (checkYield(), status.isOK() && this->isHold());
                    doAspect(GAspectType::FINISH_RUN, status);
                }
                break;
            }
            case CFunctionType::INIT: {
                status = doAspect(GAspectType::BEGIN_INIT);
                CGRAPH_FUNCTION_CHECK_STATUS
                status = init();
                doAspect(GAspectType::FINISH_INIT, status);
                break;
            }
            case CFunctionType::DESTROY: {
                status = doAspect(GAspectType::BEGIN_DESTROY);
                CGRAPH_FUNCTION_CHECK_STATUS
                status = destroy();
                doAspect(GAspectType::FINISH_DESTROY, status);
                break;
            }
            default:
                CGRAPH_RETURN_ERROR_STATUS("get function type error")
        }
    } catch (const CException& ex) {
        status = crashed(ex);
    }

    CGRAPH_FUNCTION_END
}


CBool GElement::isHold() {
    /**
     * 默认仅返回false
     * 可以根据自己逻辑，来实现"持续循环执行，直到特定条件出现的时候停止"的逻辑
     */
    return false;
}


CBool GElement::isMatch() {
    /**
     * 默认仅返回false
     * 主要面对写入 MultiCondition 的时候，做判断当前element是否被执行
     */
    return false;
}


CStatus GElement::crashed(const CException& ex) {
    return CStatus(STATUS_CRASH, ex.what());
}


CIndex GElement::getThreadIndex() {
    if (nullptr == thread_pool_) {
        return CGRAPH_SECONDARY_THREAD_COMMON_ID;    // 理论不存在的情况
    }

    auto tid = (CSize)std::hash<std::thread::id>{}(std::this_thread::get_id());
    return thread_pool_->getThreadNum(tid);
}


GElement* GElement::setThreadPool(UThreadPoolPtr ptr) {
    CGRAPH_ASSERT_NOT_NULL_THROW_ERROR(ptr)
    CGRAPH_ASSERT_INIT_THROW_ERROR(false)
    this->thread_pool_ = ptr;
    return this;
}


CVoid GElement::dump(std::ostream& oss) {
    dumpElement(oss);

    for (const auto& node : run_before_) {
        dumpEdge(oss, this, node);
    }
}


CVoid GElement::dumpEdge(std::ostream& oss, GElementPtr src, GElementPtr dst, const std::string& label) {
    if (src->isGroup() && dst->isGroup()) {
        // 在group的逻辑中，添加 cluster_ 的信息
        oss << 'p' << src << " -> p" << dst << label << "[ltail=cluster_p" << src << " lhead=cluster_p" << dst << "];\n";
    } else if (src->isGroup() && !dst->isGroup()) {
        oss << 'p' << src << " -> p" << dst << label << "[ltail=cluster_p" << src << "];\n";
    } else if (!src->isGroup() && dst->isGroup()) {
        oss << 'p' << src << " -> p" << dst << label << "[lhead=cluster_p" << dst << "];\n";
    } else {
        oss << 'p' << src << " -> p" << dst << label << ";\n";
    }
}


CVoid GElement::dumpElement(std::ostream& oss) {
    oss << 'p' << this << "[label=\"";
    if (this->name_.empty()) {
        oss << 'p' << this;    // 如果没有名字，则通过当前指针位置来代替
    } else {
        oss << this->name_;
    }

    oss << "\"];\n";
    if (this->loop_ > 1 && !this->isGroup()) {
        oss << 'p' << this << " -> p" << this << "[label=\"" << this->loop_ << "\"]" << ";\n";
    }
}


CVoid GElement::checkYield() {
    std::unique_lock<std::mutex> lk(yield_mutex_);
    this->yield_cv_.wait(lk, [this] {
        return GElementState::YIELD != cur_state_;
    });
}


CBool GElement::isGroup() {
    // 按位与 GROUP有值，表示是 GROUP的逻辑
    return (long(element_type_) & long(GElementType::GROUP)) > 0;
}


CIndex GElement::getBindingIndex() {
    return this->binding_index_;
}


CStatus GElement::buildRelation(GElementRelation& relation) {
    CGRAPH_FUNCTION_BEGIN

    relation.predecessors_ = this->dependence_;    // 前驱
    relation.successors_ = this->run_before_;    // 后继
    relation.belong_ = this->belong_;    // 从属信息

    CGRAPH_FUNCTION_END
}


CBool GElement::isSerializable() {
    return true;
}

CGRAPH_NAMESPACE_END
